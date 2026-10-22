//
// Copyright (c) 2018-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <iostream>
#include <unordered_map>
#include <boost/program_options.hpp>
#include "ampere/event_stream/event_stream_reader.h"
#define USING_CAPSTONE
#if defined(USING_CAPSTONE)
#include <platform.h>
#include <capstone.h>
static csh disasm_handle;
#endif
#define USING_ADIS
#if defined(USING_ADIS)
#include "ampere/adis/src/adis.h"
#include "ampere/archx/bitfield.h"
#if !defined(USING_CAPSTONE)
// if no capstone, always use adis
static const bool use_adis{true};
#else
static bool use_adis{false};
#endif
#endif

namespace po = boost::program_options;

/*
 * used for sizing a static array -- increase as needed
 */
const int NUM_METRICS = 64;

/*
 * default clock period in picoseconds
 */
const int CLOCK_PERIOD = 400;

/*
 * vector of metrics parsed from the cmdline
 */
std::vector<std::string> opt_v;
std::string opt_prefix;

/*
 * enable rollup of issue pipes
 */
bool opt_p = false;
const int NUM_PIPES = 12;

/*
 * This is the instruction Panthera uses on the speculative path
 * 91002c21  add   x1, x1, #0xb
 */
const std::uint32_t PAN_SPECULATIVE = 0x91002c21;

/*
 * struct that holds the stash of data for each unique PC
 */
struct IStash
{
    std::uint32_t bytes;          // raw instruction bytes
    int count_fetches{0};         // frequency of fetches (iq.write)
    int count_commits{0};         // frequency of commits
    int count_flushes{0};         // frequency of gpc.flushes
    int count_l2cmiss{0};         // frequency of L2 cache read misses (DATA, not CODE)
    int metric[NUM_METRICS]{0};   // metric array
    int count_pipe[NUM_PIPES]{0}; // frequency of issues per pipe
    int pushout_cycles{0};        // sum of commit delays
};

/*
 * Info about each uop decoded
 */
struct UopInfo
{
    std::string name{""};
    int issue_cycle{0};
    int writeback_cycle{0};
};

/*
 * map indexed by PC
 */
using InstMap = std::map<std::uint64_t, IStash>;
static InstMap instrs_data;

/*
 * map indexed by txid
 */
using TxidPCMap = std::map<std::uint64_t, std::uint64_t>;
static TxidPCMap txid_map;

/*
 * map indexed by txid
 */
using TxParentMap = std::map<std::uint64_t, std::uint64_t>;
static TxParentMap parent_txid_map;

/*
 * map indexed by txid
 */
using UopMap = std::map<std::uint64_t, UopInfo>;
static UopMap uop_data;

/*
 * isolated code for running the capstone disassembler
 */
std::string disasm(std::uint32_t bytes, std::uint64_t pc)
{
#if defined(USING_ADIS)
    if (use_adis) {
        adis::encoding_enum_t r = adis::decode(bytes);
        std::string s = adis::disasm(bytes, r);
        return s;
    }
#endif

#if defined(USING_CAPSTONE)
    size_t count(0);
    cs_insn* insn;
    char inst_dis[256];

    // place the instruction bytes into the format required by capstone
    uint8_t byte_array[4];
    byte_array[0] = bytes;
    byte_array[1] = bytes >> 8;
    byte_array[2] = bytes >> 16;
    byte_array[3] = bytes >> 24;

    count = cs_disasm(disasm_handle, byte_array, 4, pc, 0, &insn);

    if (count) {
        // format the disasm and write it to the record
        sprintf(inst_dis, "%-5s %s", insn[0].mnemonic, insn[0].op_str);

        // free memory allocated by cs_disasm()
        cs_free(insn, count);

    } else {
        sprintf(inst_dis, "[undefined opcode: %x]", bytes);
    }

    return std::string(inst_dis);
#endif
}

#if defined(USING_CAPSTONE)
/*
 * set up the capstone library for disassembly
 */
void capstone_init()
{
    cs_err err = cs_open(CS_ARCH_ARM64, CS_MODE_ARM, &disasm_handle);
    if (err) {
        printf("Failed on cs_open() with error returned: %u\n", err);
        assert(0);
    }
    cs_option(disasm_handle, CS_OPT_DETAIL, CS_OPT_ON);
}
#endif

/*
 * Print out stats about uops and latencies
 */
void uop_info_print()
{
    int total_divs{0};
    int early_exits{0};
    int saved_latency{0};
    for (auto& [_, u] : uop_data) {
        if (u.name.empty() || u.issue_cycle == 0 || u.writeback_cycle == 0)
            continue;

        bool is_div32 = (u.name == "sdiv32" || u.name == "udiv32");
        bool is_div64 = (u.name == "sdiv64" || u.name == "udiv64");

        if (is_div32 || is_div64) {
            total_divs++;
            int expected_latency = is_div32 ? 19 : 35;
            int actual_latency = u.writeback_cycle - u.issue_cycle;
            if (actual_latency < expected_latency) {
                early_exits++;
                saved_latency += (expected_latency - actual_latency);
            }
        }
    }

    if (total_divs)
        printf("Total %d integer divides, of which %d were early exits, saving %d cycles of latency\n\n\n",
               total_divs,
               early_exits,
               saved_latency);
}

/*
 * print out the table of instruction and data
 */
void table_print()
{
    std::uint64_t nextpc(0);

    int kpis = 5; // number of static perf metrics

    std::vector<int> totals;
    int total_metrics = kpis + opt_v.size() + NUM_PIPES;
    totals.resize(total_metrics);

    uop_info_print();

    printf("prefix : %s\n", opt_prefix.c_str());
    for (unsigned idx = 0; idx < opt_v.size(); idx++) {
        printf("metric%d: %s\n", idx, opt_v[idx].c_str());
    }
    printf("--------------------------------------------------------------------------------------------\n");

    printf("pc[63-0]:       pc[10-2]:   fetch:  commit:   flush: l2_miss: pushout:");

    for (unsigned idx = 0; idx < opt_v.size(); idx++) {
        printf(" metric%d:", idx);
    }

    if (opt_p) {
        printf("  SchA0:  SchB0:  SchC0:  SchD0:  SchA1:  SchB1:  SchC1:  SchD1:   SchX:   SchY:   SchZ:  SchBS:");
    }

    printf("    bytes  disassembly\n");

    for (auto i : instrs_data) {
        int size = 0;
        std::uint64_t mypc = i.first;
        IStash ii = i.second;

        if (!mypc)
            continue;

        if (!nextpc)
            nextpc = mypc;

        if (mypc != nextpc) {
            printf("...\n");
        }

        std::uint64_t lsupc = ampere::archx::Bitfield(mypc).extract_range(10, 2);

        // total counts
        size += printf("0x%016lx:%5lx:%8d:%8d", mypc, lsupc, ii.count_fetches, ii.count_commits);
        size += printf(":%8d:%8d:%8d", ii.count_flushes, ii.count_l2cmiss, ii.pushout_cycles);

        // kpis
        totals[0] += ii.count_fetches;
        totals[1] += ii.count_commits;
        totals[2] += ii.count_flushes;
        totals[3] += ii.count_l2cmiss;
        totals[4] += ii.pushout_cycles;

        for (unsigned idx = 0; idx < opt_v.size(); idx++) {
            size += printf(":%8d", ii.metric[idx]);
            totals[idx + kpis] += ii.metric[idx];
        }

        if (opt_p) {
            for (unsigned idx = 0; idx < NUM_PIPES; idx++) {
                size += printf(":%7d", ii.count_pipe[idx]);
                totals[idx + opt_v.size() + kpis] += ii.count_pipe[idx];
            }
        }

        std::vector<std::string> disasm_strs;
        if (ii.bytes) {
            disasm_strs.emplace_back(disasm(ii.bytes, mypc));
        } else if (auto uop = uop_data.find(mypc); uop != uop_data.end()) {
            // no instruction bytes, use uop disasm instead
            disasm_strs.emplace_back(uop->second.name);
        } else {
            // no uop, try to infer uop based upon txid (assuming txid was used as PC)
            std::string first_uop_name;
            for (const auto& [u_txid, p_txid] : parent_txid_map) {
                if ((p_txid << 2) != mypc)
                    continue;

                if (auto uop = uop_data.find(u_txid); uop != uop_data.end()) {
                    if (first_uop_name.empty())
                        first_uop_name = uop->second.name;
                    else if (first_uop_name == uop->second.name)
                        break;

                    disasm_strs.emplace_back(uop->second.name);
                }
            }
        }

        if (disasm_strs.empty())
            disasm_strs.emplace_back("unknown");

        unsigned index = 0;
        for (const auto& disasm_str : disasm_strs) {
            if (index++)
                printf("%*s: %08x  %s\n", size, " ", ii.bytes, disasm_str.c_str());
            else
                printf(": %08x  %s\n", ii.bytes, disasm_str.c_str());
        }

        nextpc = mypc + 0x4;
    }
    printf("--------------------------------------------------------------------------------------------\n");

    printf("           SUMMARY:     :   fetch:  commit:   flush: l2_miss: pushout:");

    for (unsigned idx = 0; idx < opt_v.size(); idx++) {
        printf(" metric%d:", idx);
    }

    if (opt_p) {
        printf("  SchA0:  SchB0:  SchC0:  SchD0:  SchA1:  SchB1:  SchC1:  SchD1:   SchX:   SchY:   SchZ:  SchBS:");
    }

    printf("\n            totals:     :");

    for (int idx = 0; idx < total_metrics - NUM_PIPES; idx++) {
        printf("%8d:", totals[idx]);
    }

    if (opt_p) {
        for (int idx = total_metrics - NUM_PIPES; idx < total_metrics; idx++) {
            printf("%6d:", totals[idx]);
        }
    }

    printf("\n\n");
}

struct DebugPrinter
{
    bool enabled{false};

    template<typename... Args>
    void print(const char* msg, Args&&... args) const
    {
        if (enabled)
            fprintf(stderr, msg, std::forward<Args>(args)...);
    }
};

/*
 * main
 */
int main(int argc, char* argv[])
{
    int opt_end_inst(0), opt_clock(0);
    bool stream_is_rtl{false};

    po::options_description desc("Allowed options");
    desc.add_options()("help,h", "produce help message")(
        "clock,c", po::value<int>(&opt_clock)->default_value(CLOCK_PERIOD), "clock period in picoseconds")(
        "instrs,e", po::value<int>(&opt_end_inst)->default_value(30000), "instructions to rollup from start")(
        "prefix,x",
        po::value<std::string>(&opt_prefix)->default_value("sys.soc_0.core_0"),
        "top level prefix to use for all metrics")(
        "metric,m", po::value<std::vector<std::string>>(), "vector knob of metric strings to rollup")(
        "pipes,p", "roll up issue pipes")
#if defined(USING_ADIS)
        ("adis,a", "Use ADIS for disassembly")
#endif
            ("debug,d", "Emit debug info to stderr");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (1 == argc || vm.count("help") || !(vm.count("metric") || vm.count("pipes") || vm.count("instrs"))) {
        std::cerr << desc << "\n";
        std::cerr << "Usage: " << std::endl;
        std::cerr << "  " << argv[0] << " [opts] [path to stream file]" << std::endl;
        return 1;
    }

#if defined(USING_CAPSTONE) && defined(USING_ADIS)
    // only look at option if also using capstone
    if (vm.count("adis")) {
        use_adis = true;
    }
#endif

    opt_p = vm.count("pipes");
    if (vm.count("metric"))
        opt_v = vm["metric"].as<std::vector<std::string>>();

    if (opt_v.size() > NUM_METRICS) {
        std::cerr << argv[0] << ": ERROR: Maximum number of allowed metrics is " << NUM_METRICS << std::endl;
        exit(EXIT_FAILURE);
    }
#if defined(USING_CAPSTONE)
    capstone_init();
#endif
    int instruction_count(0);
    std::uint64_t prev_commit_cycle(0);

    ampere::event_stream::EventStreamReader es_reader(argv[argc - 1]);
    ampere::event_stream_proto::Record record;
    std::unordered_map<int, ampere::event_stream_proto::Definition> definitions;
    DebugPrinter debug{static_cast<bool>(vm.count("debug"))};

    int id_txid(0), id_pc(0), id_iqwrite(0), id_instcommit(0), id_inst_bytes(0), id_parent(0);
    int id_flush(0), id_uopissue(0), id_pipe(0), id_l2wrhit(0), id_l2wracc(0), id_l2rdhit(0), id_l2rdacc(0);
    int id_uopname(0), id_writeback(0);
    int id_rtl_ixn_commit_pc(0), id_rtl_ixn_commit_ixn(0);
    int id_end_transaction(0);
    int id_metric[NUM_METRICS]{0};

    while (true) {
        // start to read through the file
        // if we reached the end, break out
        if (!es_reader.read(record))
            break;

        if (!record.has_event() && !record.has_definition()) {
            if (record.has_control()) {
                std::map<const char*, int32_t> essential_definitions = {
                    {"txid", id_txid},
                    {"parent", id_parent},
                    {"end_transaction", id_end_transaction},
                    {"disasm", id_uopname},
                    {"instcommit", id_instcommit || id_rtl_ixn_commit_ixn},
                    {"pc", id_pc || id_rtl_ixn_commit_pc},
                };

                for (const auto& [name, value] : essential_definitions) {
                    if (!value)
                        std::cerr << "warning: " << name << " undefined (not seen/defined in ES file)" << std::endl;
                }
            }

            continue;
        }

        // first discover the definitions. We are looking for some anchors in
        // particular and need to save off their IDs.
        //
        if (record.has_definition()) {
            const auto& definition = record.definition();
            auto def_id = definition.id();
            definitions[def_id] = definition;

            // txid, parent
            // program_counter (pc in RTL)
            // sys.soc_0.core_0.icf.inst_bytes (instruction in RTL)
            // sys.soc_0.core_0.icf.iq_write (sys.soc_0.core_0.icf.fetch in RTL)
            // sys.soc_0.core_0.ixu.ixu_pipe (ixu_pipe in RTL)
            // sys.soc_0.core_0.ixu.uop_issue
            // sys.soc_0.core_0.l2c.read_pipeline.access and sys.soc_0.core_0.l2c.read_pipeline.hit
            // sys.soc_0.core_0.gpc.flush
            // sys.soc_0.core_0.gpc.instruction_commit
            //
            // these are the ones to look for here
            //
            if (definition.name() == "txid") {
                id_txid = def_id;
            } else if (definition.name() == "parent") {
                id_parent = def_id;
            } else if (definition.name() == "end_transaction") {
                id_end_transaction = def_id;

            } else if (definition.name() == std::string(opt_prefix + ".icf.pc")) { // RTL
                id_pc = def_id;
            } else if (definition.name() == "program_counter") { // PAN
                id_pc = def_id;

            } else if (definition.name() == "ixu_pipe") { // RTL
                id_pipe = def_id;
            } else if (definition.name() == std::string(opt_prefix + ".ixu.ixu_pipe")) { // PAN
                id_pipe = def_id;

            } else if (definition.name() == std::string(opt_prefix + ".icf.instruction")) { // RTL
                id_inst_bytes = def_id;
            } else if (definition.name() == std::string(opt_prefix + ".icf.inst_bytes")) { // PAN
                id_inst_bytes = def_id;
            } else if (definition.name() == std::string(opt_prefix + ".pfe.inst_bytes")) { // PAN
                id_inst_bytes = def_id;

            } else if (definition.name() == std::string(opt_prefix + ".icf.fetch")) { // RTL
                id_iqwrite = def_id;
            } else if (definition.name() == std::string(opt_prefix + ".icf.iq_write")) { // PAN
                id_iqwrite = def_id;
            } else if (definition.name() == std::string(opt_prefix + ".pfe.iq_write")) { // PAN
                id_iqwrite = def_id;

            } else if (definition.name() == std::string(opt_prefix + ".l2c.read_pipeline.request")) { // RTL
                id_l2rdacc = def_id;
            } else if (definition.name() == std::string(opt_prefix + ".l2c.access")) { // PAN
                id_l2rdacc = def_id;

            } else if (definition.name() == std::string(opt_prefix + ".l2c.write_pipeline.request")) { // RTL
                id_l2wracc = def_id;
            } else if (definition.name() == std::string(opt_prefix + ".l2c.write_pipeline.hit")) { // RTL
                id_l2wrhit = def_id;

            } else if (definition.name() == std::string(opt_prefix + ".ixu.uop_writeback")) { // RTL
                id_writeback = def_id;
                stream_is_rtl = true;
            } else if (definition.name() == std::string(opt_prefix + ".ixu.uop_data_available")) { // PAN
                id_writeback = def_id;

            } else if (definition.name() == std::string(opt_prefix + ".idr.uopcode_disasm")) {
                id_uopname = def_id;

            } else if (definition.name() == std::string(opt_prefix + ".l2c.read_pipeline.hit")) { // RTL (and old PAN)
                id_l2rdhit = def_id;
            } else if (definition.name() == std::string(opt_prefix + ".l2c.hit")) { // PAN (new L2)
                id_l2rdhit = def_id;
            } else if (definition.name() == std::string(opt_prefix + ".ixu.uop_issue")) {
                id_uopissue = def_id;
            } else if (definition.name() == std::string(opt_prefix + ".gpc.instruction_commit")) {
                // this is the general event for committing an instruction on both RTL and PAN
                id_instcommit = def_id;
            } else if (definition.name() == std::string(opt_prefix + ".gpc.flush")) {
                id_flush = def_id;
            } else if (definition.name() == std::string(opt_prefix + ".gpc.commit_instruction")) { // RTL
                // this is the RTL event data for the instruction bytes for the committed instr
                id_rtl_ixn_commit_ixn = def_id;
            } else if (definition.name() == std::string(opt_prefix + ".gpc.commit_pc")) { // RTL
                // this is the RTL event data for the committed PC
                id_rtl_ixn_commit_pc = def_id;

                // These two id_rtl def_id's are so we can pull an instruction's PC and instruction encoding
                // from the instruction commit event, since we may not have an instruction fetch event when looping
            }

            // look for all the variable metrics
            for (unsigned idx = 0; idx < opt_v.size(); idx++) {
                if (definition.name() == std::string(opt_prefix + "." + opt_v[idx])) {
                    id_metric[idx] = def_id;
                }
            }
        } else if (record.has_event()) {
            // now go through the event stream to look for all the IDs we
            // discovered above, and put them in a map

            const auto& event = record.event();

            std::uint32_t e_inst_bytes(0);
            std::uint64_t e_pc(0), e_txid(0), e_parent(0);
            std::int32_t e_pipe(0);
            std::string e_uopname;
            bool e_hit(false);

            // grab values first
            for (int i = 0; i < event.values_size(); i++) {
                const auto& value = event.values(i);
                auto definition = definitions[value.definition_id()];

                if (value.definition_id() == id_txid) {
                    e_txid = value.uint_value();
                } else if (value.definition_id() == id_pc || value.definition_id() == id_rtl_ixn_commit_pc) {
                    e_pc = value.uint_value();
                } else if (value.definition_id() == id_inst_bytes || value.definition_id() == id_rtl_ixn_commit_ixn) {
                    e_inst_bytes = value.uint_value();
                } else if (value.definition_id() == id_parent) {
                    e_parent = value.uint_value();
                } else if (value.definition_id() == id_l2rdhit || value.definition_id() == id_l2wrhit) {
                    e_hit = value.uint_value();
                } else if (opt_p && value.definition_id() == id_pipe) {
                    e_pipe = value.uint_value();
                } else if (value.definition_id() == id_uopname) {
                    e_uopname = value.string_value();
                }
            }

            // reuse txid as PC in cases where PC does not exist (i.e. RTL in loop-mode)
            if (!id_pc && event.definition_id() == id_instcommit)
                e_pc = e_txid << 2;

            if (e_txid) {
                if (e_pc)
                    txid_map[e_txid] = e_pc;
                else if (auto tx_pc = txid_map.find(e_txid); tx_pc != txid_map.end())
                    e_pc = tx_pc->second;
            }

            // establish parenting
            if (e_parent && e_txid)
                parent_txid_map[e_txid] = e_parent;

            // find/populate pc via parenting
            for (auto pc_txid = parent_txid_map.find(e_txid); pc_txid != parent_txid_map.end() && !e_pc;
                 pc_txid = parent_txid_map.find(pc_txid->second))
            {
                e_pc = txid_map[pc_txid->second];
                if (pc_txid->second == e_txid)
                    break;
                if (e_pc)
                    txid_map[e_txid] = e_pc;
            }

            if (!e_uopname.empty()) {
                // Strip leading UOP_ and convert everything to lower case, so RTL and panthera uop names match
                if (e_uopname.starts_with("UOP_"))
                    e_uopname = e_uopname.substr(4);
                for (char& i : e_uopname)
                    i = std::tolower(i);
                uop_data[e_txid].name = e_uopname;

                debug.print("UOP %s,\tTXID=%llu,\tPC=%llx\n", e_uopname.c_str(), e_txid, e_pc);
            }

            // mine event data
            if (event.definition_id() == id_end_transaction) {
                if (e_txid) {
                    txid_map.erase(e_txid);
                    parent_txid_map.erase(e_txid);
                }

            } else if (event.definition_id() == id_iqwrite) {
                auto* ii = &instrs_data[e_pc];
                if (e_inst_bytes && (0 == ii->count_fetches || PAN_SPECULATIVE == ii->bytes)) {
                    ii->bytes = e_inst_bytes;
                }
                ii->count_fetches++;

            } else if (event.definition_id() == id_instcommit) {
                auto* ii = &instrs_data[e_pc];
                if (e_inst_bytes && 0 == ii->bytes) {
                    ii->bytes = e_inst_bytes;
                }
                ii->count_commits++;
                instruction_count++;

                // start counting pushout only after first commit
                if (0 == prev_commit_cycle)
                    prev_commit_cycle = event.time();

                // accumulated commit pushout per instruction
                int pushout = (event.time() - prev_commit_cycle) / opt_clock;
                prev_commit_cycle = event.time();
                ii->pushout_cycles += pushout;

            } else if (event.definition_id() == id_l2rdacc) {
                auto* ii = &instrs_data[e_pc];
                if (!e_hit)
                    ii->count_l2cmiss++;

            } else if (event.definition_id() == id_l2wracc) {
                auto* ii = &instrs_data[e_pc];
                if (!e_hit)
                    ii->count_l2cmiss++;

            } else if (event.definition_id() == id_flush) {
                auto* ii = &instrs_data[e_pc];
                ii->count_flushes++;

            } else if (event.definition_id() == id_uopissue) {
                if (opt_p) {
                    auto* ii = &instrs_data[e_pc];
                    ii->count_pipe[e_pipe]++;
                }

                auto* u = &uop_data[e_txid];
                auto issue_cycle = event.time() / opt_clock;
                // uops may issue multiple times due to cancels, so keep only the last one
                if (issue_cycle > u->issue_cycle)
                    u->issue_cycle = issue_cycle;
            } else if (event.definition_id() == id_writeback) {
                auto* u = &uop_data[e_txid];
                u->writeback_cycle = event.time() / opt_clock;
                // RTL uop_writeback event happens one cycle after panthera uop_data_available event,
                // so adjust for that here so latencies come out the same
                if (stream_is_rtl)
                    u->writeback_cycle--;
            }

            // variable metrics
            for (unsigned idx = 0; idx < opt_v.size(); idx++) {
                if (event.definition_id() == id_metric[idx]) {
                    auto* ii = &instrs_data[e_pc];
                    ii->metric[idx]++;
                }
            }
        }

        record.Clear();

        if (opt_end_inst <= instruction_count)
            break;
    }

    table_print();
    printf("instruction_count = %d\n", instruction_count);

    return 0;
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
