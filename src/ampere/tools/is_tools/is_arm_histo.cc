//
// Copyright (c) 2021-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

// Histogram ARM instructions. Optionally log capstone decode failures to a file.
// Computes subhistograms for the uop sequences emitted for each instruction.
// Emits json to stdout.

#include <nlohmann/json.hpp>

// #define USING_CAPSTONE
#if defined(USING_CAPSTONE)
#include <capstone.h>
#include <platform.h>
#else
#define USING_ADIS
#include "ampere/adis/src/adis.h"
#endif
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <fmt/ranges.h>
#include <boost/filesystem/path.hpp>
#include "ampere/archx/instruction_is.h"
#include "ampere/instruction_stream/instruction_stream.h"
#include "ampere/archx/uopcodes.h"
#include "ampere/archx/uop.h"

#include <cstdlib>
#include <cstdint>
#include <stdexcept>
#include <map>
#include <deque>

#include "chip-decoder-map.h"
typedef uint_fast32_t uint_t;

using json = nlohmann::json;
namespace fs = boost::filesystem;
using namespace ampere;

#if defined(USING_CAPSTONE)
static csh disasm_handle; // capstone
#define MAX_INSTR_ID ARM64_INS_ENDING
#else
#define MAX_INSTR_ID ADIS_MAX_INST
#endif

struct uop_histo_t
{
    std::map<std::string, uint64_t> histo;

    static std::string make_key(const ampere::archx::UopSet& uops)
    {
        std::string s;
        bool first = true;
        for (auto& uop : uops) {
            if (first)
                first = false;
            else
                s += ";";
            s += fmt::format("{}", uop->opcode);
        }
        return s;
    }

    void add(const ampere::archx::UopSet& uops)
    {
        std::string key = make_key(uops);
        auto exists = histo.find(key);
        if (exists == histo.end()) {
            histo[key] = 1;
        } else {
            histo[key] += 1;
        }
    }

    json dump() const
    {
        json js;
        for (auto& i : histo) {
            js[i.first] = i.second;
        }
        return js;
    }
};

struct stats_t
{
    uint64_t instructions{0};
    uint64_t instr_hist[MAX_INSTR_ID];
    uop_histo_t uop_histo[MAX_INSTR_ID];
    std::string names[MAX_INSTR_ID];

    stats_t() : instructions(0) { memset(instr_hist, 0, sizeof(instr_hist)); }
};

struct inst_info_t
{
    uint_t id;
    std::string mnemonic;
};

static struct inst_info_t get_opcode_info(uint32_t itext, uint64_t pc)
{
    struct inst_info_t rc;
#if defined(USING_CAPSTONE)
    size_t count(0);
    cs_insn* insn;

    // place the instruction itext into the format required by capstone
    uint8_t byte_array[4];
    byte_array[0] = itext;
    byte_array[1] = itext >> 8;
    byte_array[2] = itext >> 16;
    byte_array[3] = itext >> 24;

    count = cs_disasm(disasm_handle, byte_array, 4, pc, 0, &insn);
    if (count) {
        rc.id = insn[0].id;
        rc.mnemonic = insn[0].mnemonic;
    } else {
        rc.id = 0;
        rc.mnemonic = "invalid opcode";
    }
    cs_free(insn, count);
#else
    rc.id = static_cast<uint_t>(adis::decode(itext));
    rc.mnemonic = adis::encoding_name(static_cast<adis::encoding_enum_t>(rc.id));
#endif
    return rc;
}

static stats_t histogram_instructions(const fs::path& stream, const fs::path& decode_failures)
{
    auto reader = new is::InstructionStreamReader(stream);
    is::Instruction instruction;
    std::FILE* f = nullptr;

    stats_t stats;
    fmt::print(stderr, "Working: ");
    uint_t i = 0;
    while (reader->read(instruction)) {
        i++;
        const uint_t mil = 1000 * 1000;
        if ((i % mil) == 0) {
            fmt::print(stderr, "{}M ", i / mil);
        }
        ++stats.instructions;

        inst_info_t iinfo = get_opcode_info(instruction.opcode(), instruction.program_counter().va());

        assert(iinfo.id < MAX_INSTR_ID);
        stats.instr_hist[iinfo.id]++;
        if (stats.names[iinfo.id].size() == 0)
            stats.names[iinfo.id] = iinfo.mnemonic;

        {
            auto uops = ampere::archx::Instruction::decode(instruction.opcode());
            stats.uop_histo[iinfo.id].add(uops);
        }

        // log decode failures if filename is provided - warning can be LARGE
        if (iinfo.id == 0 && (f || decode_failures.size() > 0)) {
            if (f == nullptr) {
                f = std::fopen(decode_failures.native().c_str(), "w");
                if (!f) {
                    throw fmt::system_error(errno, "Could not open decode failure file {}", decode_failures.native());
                }
            }
            fmt::print(f, "{:08x}\n", instruction.opcode());
        }
    }

    fmt::print(stderr, "done\n");
    if (f) {
        std::fclose(f);
    }
    delete reader;
    return stats;
}

static void print_counts_per_instr(const stats_t& stats)
{
    // dump the JSON
    json jsv;
    uint_t slot = 0;
    for (uint_t j = 0; j < MAX_INSTR_ID; j++) {
        uint64_t total = stats.instr_hist[j];
        if (total) {
            jsv["icount_histogram"][slot++] = {
                {"instr", stats.names[j]}, {"freq_total", total}, {"uop_histo", stats.uop_histo[j].dump()}};
        }
    }
    fmt::print("{}\n", jsv.dump(4));
}

static void print_counts(const stats_t& stats)
{
    print_counts_per_instr(stats);
}

static void usage(char const* prog)
{
    fmt::print(stderr, "Usage: {} [options] trace-stream\n", prog);
    char const* umsg[] = {"-c CHIP             -- specify chip for decoders. Default: furiosa",
                          "-h/--help           -- this help message",
                          "-f/--fail FILENAME  -- file name for logging decode failures",
                          nullptr};

    for (int i = 0; umsg[i]; i++) {
        fmt::print(stderr, "\t{}\n", umsg[i]);
    }
    exit(EXIT_FAILURE);
}

static std::string get_string(std::deque<std::string>& cmdline)
{
    if (cmdline.empty()) {
        throw std::runtime_error("Need an string argument for option");
    }
    std::string s = cmdline.front();
    cmdline.pop_front();
    return s;
}

int main(int argc, char** argv)
{
    fs::path instruction_stream;
    fs::path decode_failures;
    std::vector<std::string> decoder_names;
    std::deque<std::string> cmdline(argv + 1, argv + argc);
    bool got_chip = false;
    while (!cmdline.empty()) {
        auto arg = cmdline.front();
        cmdline.pop_front();
        if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
        } else if (arg == "-f" || arg == "--fail") {
            decode_failures = get_string(cmdline);
        } else if (arg == "-c") {
            if (got_chip) {
                throw std::runtime_error(fmt::format("Only one chip (-c) arg allowed"));
            }
            got_chip = true;
            auto chip = get_string(cmdline);
            decoder_names = is_tools::get_decoders(chip);
        } else if (instruction_stream.size() == 0) {
            instruction_stream = arg;
        } else {
            usage(argv[0]);
        }
    }
    if (instruction_stream.size() == 0) {
        throw std::runtime_error(
            fmt::format("Missing required trace path. "
                        "See help message with --help option."));
    }

    if (!decoder_names.size()) {
        decoder_names.emplace_back("furiosa");
    }
    fmt::print(stderr, "Setting decoders to: [{}]\n", fmt::join(decoder_names, ", "));
    ampere::archx::Instruction::set_decoders(decoder_names);

#if defined(USING_CAPSTONE)
    // set up capstone for disassembly
    cs_err err = cs_open(CS_ARCH_ARM64, CS_MODE_ARM, &disasm_handle);
    if (err) {
        throw std::runtime_error(fmt::format("Failed on cs_open() with error returned: {}", err));
    }
#endif
    print_counts(histogram_instructions(instruction_stream, decode_failures));
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
