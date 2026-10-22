//
// Copyright (c) 2021-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/archx/addr.h"
#include "ampere/archx/furiosa_encode.h"
#include "ampere/archx/instruction_simple.h"
#include "ampere/mesh/injector/uop_source_prog.h"
#include "ampere/soc/warmup_info.h"

namespace ampere::mesh {

using namespace ampere::archx;
using namespace ampere::sc;

UopSourceProg::UopSourceProg(const sc_core::sc_module_name& name) : UopSource{name}
{
    setup_parameters();

    if (program.val().count("generators"))
        setup_generators(program.val()["generators"]);

    loop_count = maybe_get_uint64(program.val(), "loop", 1);
    ping_pong = maybe_get_uint64(program.val(), "ping_pong_mask", 0);

    compile();
}

void UopSourceProg::setup_parameters()
{
    for (auto& param : parameters.val()) {
        auto name = param["name"].get<std::string>();
        auto description = param["description"].get<std::string>();
        auto defval = param["default"];

        dynamic_parameters.emplace(
            std::piecewise_construct, std::forward_as_tuple(name), std::forward_as_tuple(name, description, defval));
    }
}

void UopSourceProg::end_of_elaboration()
{
    if (enabled) {
        if (decoders.val().empty())
            AMPERE_ERROR("no decoders were specified");

        archx::Instruction::set_decoders(decoders);

        if (enable_warmup)
            do_warmup();
    }
    if (warmup_only)
        do_warmup();
}

void UopSourceProg::start_of_simulation()
{
    if (enable_events_on_start)
        get_environment()->event_stream->enable();
}

void UopSourceProg::compile()
{
    if (!program.val().count("code"))
        return;

    ProgramCounter pc{0x1000_pc};

    auto code = program.val()["code"].get<std::vector<json>>();

    for (auto& insn : code) {
        Insn compiled_insn;

        compiled_insn.pc = ProgramCounter{maybe_get_uint64(insn, "pc", (pc + 4).value())};
        if (insn.count("op"))
            compiled_insn.opcode =
                furiosa_encode(insn["op"].get<std::string>(), insn["fields"].get<std::map<std::string, uint32_t>>());
        else {
            compiled_insn.opcode = insn["type"].get<std::string>() == "Read" ? chi::IOType::Read : chi::IOType::Write;
            set_tlp_size(compiled_insn, insn);
        }

        set_repeat(compiled_insn, insn);
        set_barrier(compiled_insn, insn);
        set_address(compiled_insn, insn);

        if (compiled_insn.repeat_count != 0)
            block["code"].push_back(compiled_insn);
    }

    if (block["code"].size())
        repeat_count = block["code"][0].repeat_count;
}

static soc::WarmupInfo::Action string_to_action(const std::string& s)
{
    if (s == "read") {
        return soc::WarmupInfo::READ;
    } else if (s == "read_unique") {
        return soc::WarmupInfo::READ_UNIQUE;
    } else if (s == "write") {
        return soc::WarmupInfo::WRITE;
    } else {
        AMPERE_FATAL("unrecognized action {}", s);
        return soc::WarmupInfo::READ;
    }
}

void UopSourceProg::do_warmup()
{
    uint64_t num_warmup_transactions = 0;

    if (!warmup.val().empty()) {
        for (auto& entry : warmup.val()) {
            PhysAddr start_addr;
            PhysAddr stop_addr;

            if (entry.contains("address")) {
                start_addr = PhysAddr{get_uint64(entry["address"])};
                stop_addr = start_addr;
                ++num_warmup_transactions;
            } else if (entry.contains("start_address")) {
                start_addr = PhysAddr{get_uint64(entry["start_address"])};
                stop_addr = PhysAddr{get_uint64(entry["stop_address"])};
                num_warmup_transactions += (stop_addr == start_addr) ? 0 : ((stop_addr - start_addr - 1) / 64 + 1);
            }

            std::vector<soc::WarmupInfo::Action> actions;

            if (entry["action"].is_string()) {
                auto s = get_str(entry["action"]);
                actions.push_back(string_to_action(s));
            } else if (entry["action"].is_array()) {
                for (auto& act : entry["action"])
                    actions.push_back(string_to_action(act.get<std::string>()));
            }

            auto warmup_entry = WarmupEntry{start_addr, stop_addr, actions};

            if (entry.contains("region"))
                warmup_entry.set_region(get_str(entry["region"]));

            warmup_table.push_back(warmup_entry);
        }
    } else {
        if (!warmup_ms.val().empty()) {
            std::string filename = warmup_ms.val();
            AMPERE_INFO("Warmstart with file {}", filename);
        }
    }
    if (num_warmup_transactions || !warmup_ms.val().empty()) {
        auto gen_warmup = std::bind(&UopSourceProg::gen_warmup, this);
        config->register_warmup(num_warmup_transactions, gen_warmup);
    }
}

std::optional<soc::WarmupInfo> UopSourceProg::gen_warmup()
{
    if (!warmup_ms.val().empty())
        return gen_warmup_ms();
    else
        return gen_warmup_table();
}

std::optional<soc::WarmupInfo> UopSourceProg::gen_warmup_ms()
{
    auto reader = ms_warmup.ms_reader.get();
    if (!reader->read(ms_warmup.ms_access))
        return {};
    auto address = PhysAddr(ms_warmup.ms_access.pa());
    soc::WarmupInfo::Action action;
    if (ms::Access_Type_WRITE == ms_warmup.ms_access.type())
        action = string_to_action("write");
    else
        action = string_to_action("read");

    // clear out to read next transaction
    ms_warmup.ms_access.Clear();

    return soc::WarmupInfo{address, action, node_id};
}

std::optional<soc::WarmupInfo> UopSourceProg::gen_warmup_table()
{
    if (warmup_index == warmup_table.size())
        return {};

    auto& entry = warmup_table[warmup_index];
    auto& action = entry.actions[action_index];

    auto address = entry.start_addr;
    entry.start_addr += 64;

    if (++action_index >= entry.actions.size())
        action_index = 0;

    if (entry.start_addr >= entry.stop_addr)
        ++warmup_index;

    if (paging.is_connected()) {
        auto region = entry.get_region();
        if (region.empty())
            region = name();
        address = paging->translate(VirtAddr(address), region);
    }

    return soc::WarmupInfo{address, action, node_id};
}

std::optional<IOop> UopSourceProg::fetch_io()
{
    if (stopped)
        return {};

    if (block_index == block["code"].size()) {
        if (--loop_count) {
            block_index = 0;
        } else {
            stopped = true;
            return {};
        }
    }
    auto insn = block["code"][block_index];
    AMPERE_ASSERT(std::holds_alternative<chi::IOType>(insn.opcode), "opcode fields are incorrect in json");
    auto addr = get_address(insn);

    IOop io_op(addr, std::get<chi::IOType>(insn.opcode), insn.tlp_size, insn.barrier);

    if (--repeat_count == 0) {
        repeat_count = block["code"][(++block_index) % (block["code"].size())].repeat_count;
        if (repeat_count == 0)
            block_index = (block_index + 1) % (block["code"].size());
    }

    return io_op;
}

std::optional<IOop> UopSourceProg::fetch_read_io()
{
    if (stopped)
        return {};

    if (read_block_idx == block["code"].size()) {
        if (--loop_count) {
            read_block_idx = 0;
        } else {
            stopped = true;
            return {};
        }
    }
    auto insn = block["code"][read_block_idx];
    size_t rr = 0;
    while (std::get<chi::IOType>(insn.opcode) != chi::IOType::Read && rr != block["code"].size()) {
        read_block_idx = (read_block_idx + 1) % (block["code"].size());
        insn = block["code"][read_block_idx];
        rr++;
    }

    if (std::get<chi::IOType>(insn.opcode) != chi::IOType::Read)
        return {};

    AMPERE_ASSERT(std::holds_alternative<chi::IOType>(insn.opcode), "opcode fields are incorrect in json");
    auto addr = get_address(insn);

    IOop io_op(addr, std::get<chi::IOType>(insn.opcode), insn.tlp_size, insn.barrier);

    if (--repeat_count == 0) {
        repeat_count = block["code"][(++read_block_idx) % (block["code"].size())].repeat_count;
        if (repeat_count == 0)
            read_block_idx = (read_block_idx + 1) % (block["code"].size());
    }

    return io_op;
}

std::optional<IOop> UopSourceProg::fetch_write_io()
{
    if (stopped)
        return {};

    if (write_block_idx == block["code"].size()) {
        if (--loop_count) {
            write_block_idx = 0;
        } else {
            stopped = true;
            return {};
        }
    }
    auto insn = block["code"][write_block_idx];
    size_t rr = 0;
    while (std::get<chi::IOType>(insn.opcode) != chi::IOType::Write && rr != block["code"].size()) {
        write_block_idx = (write_block_idx + 1) % (block["code"].size());
        insn = block["code"][write_block_idx];
        rr++;
    }
    if (std::get<chi::IOType>(insn.opcode) != chi::IOType::Write)
        return {};

    AMPERE_ASSERT(std::holds_alternative<chi::IOType>(insn.opcode), "opcode fields are incorrect in json");
    auto addr = get_address(insn);

    IOop io_op(addr, std::get<chi::IOType>(insn.opcode), insn.tlp_size, insn.barrier);

    if (--repeat_count == 0) {
        repeat_count = block["code"][(++write_block_idx) % (block["code"].size())].repeat_count;
        if (repeat_count == 0)
            write_block_idx = (write_block_idx + 1) % (block["code"].size());
    }

    return io_op;
}

std::pair<chi::IOType, uint64_t> UopSourceProg::peek_io()
{
    size_t orig_block_index = block_index;
    if (block_index == block["code"].size())
        block_index = 0;
    auto insn = block["code"][block_index];
    AMPERE_ASSERT(std::holds_alternative<chi::IOType>(insn.opcode), "opcode fields are incorrect in json");
    block_index = orig_block_index;
    return std::make_pair(std::get<chi::IOType>(insn.opcode), insn.tlp_size);
}

std::optional<Uop> UopSourceProg::fetch_uop()
{
    bool is_barrier = false;
    if (stopped)
        return {};

    if (uop_index == uop_set.size()) {
        if (block_index == block["code"].size()) {
            ev_loop();
            if (--loop_count) {
                block_index = 0;
            } else {
                stopped = true;
                last_uop_id = uop_id - 1;
                return {};
            }
        }

        auto& insn = block["code"][block_index];
        is_barrier = insn.barrier;

        if (--repeat_count == 0) {
            repeat_count = block["code"][(++block_index) % (block["code"].size())].repeat_count;
            if (repeat_count == 0)
                block_index = (block_index + 1) % (block["code"].size());
        }

        std::shared_ptr<InstructionSimple> insn_ptr;

        auto va = VirtAddr(get_address(insn)); // Really a PhysAddr, but InstructionSimple takes a VirtAddr
        AMPERE_ASSERT(std::holds_alternative<uint32_t>(insn.opcode), "opcode fields are incorrect in json");
        insn_ptr = std::make_shared<InstructionSimple>(insn.pc, std::get<uint32_t>(insn.opcode), va);

        insn_ptr->set_decoder_uarch(UopUarch::FURIOSA);
        archx_instruction = insn_ptr;

        uop_set = archx_instruction->decode();
        AMPERE_ASSERT(!uop_set.empty(), "instruction did not decode");
        uop_index = 0;

        ++instructions;
    }

    auto uop = make<Uop>(++uop_id, std::move(uop_set[uop_index++]), archx_instruction);
    uop->is_barrier = is_barrier;

    return uop;
}

void UopSourceProg::commit_uop(Uop uop)
{
    if (uop->id == last_uop_id) {
        AMPERE_INFO("Stop performance measurement after {} instruction @ {}", instructions, sc_core::sc_time_stamp());
        sc_core::sc_stop();
    }
}

void UopSourceProg::set_repeat(Insn& compiled_insn, const json& insn)
{
    compiled_insn.repeat_count = maybe_get_uint64(insn, "repeat", 1);
}

void UopSourceProg::set_barrier(Insn& compiled_insn, const json& insn)
{
    compiled_insn.barrier = maybe_get_bool(insn, "barrier");
}

void UopSourceProg::set_tlp_size(Insn& compiled_insn, const json& insn)
{
    compiled_insn.tlp_size = maybe_get_uint64(insn, "tlp_size", 1);
}

void UopSourceProg::set_address(Insn& compiled_insn, const json& insn)
{
    compiled_insn.pa = 0_pa;
    compiled_insn.generator = nullptr;
    compiled_insn.advance = false;

    if (!insn.count("pa"))
        return;

    auto& pa = insn["pa"];

    if (pa.is_string()) {
        auto sval = pa.get<std::string>();

        AMPERE_ASSERT(!sval.empty(), "physical address is empty string");
        if (isdigit(sval[0]) || sval[0] == '@') {
            compiled_insn.pa = PhysAddr{get_uint64(sval)};
        } else {
            if (sval.back() == '+') {
                compiled_insn.advance = true;
                sval = sval.substr(0, sval.size() - 1);
            }

            try {
                compiled_insn.generator = generators.at(sval).get();
            } catch (std::out_of_range&) {
                AMPERE_ERROR("no such address generator named \"{}\"", sval);
            }
        }
    } else {
        compiled_insn.pa = PhysAddr{get_uint64(pa)};
    }
}

PhysAddr UopSourceProg::get_address(const Insn& insn)
{
    if (insn.pa != 0_pa)
        return insn.pa;

    if (insn.generator) {
        auto addr = insn.generator->get_addr();
        if (insn.advance) {
            if (insn.tlp_size)
                insn.generator->advance(insn.tlp_size);
            else
                insn.generator->advance();
        }

        if (paging.is_connected()) {
            auto region = insn.generator->get_region();
            if (region.empty())
                region = name();
            return paging->translate(VirtAddr(addr), region);
        } else {
            return addr;
        }
    }

    return 0_pa;
}

void UopSourceProg::setup_generators(const json& generator_list)
{
    for (auto& gen : generator_list) {
        auto name = gen["name"].get<std::string>();
        auto type = gen["type"].get<std::string>();

        if (type == "strided") {
            if (gen.count("stop")) {
                auto g = std::make_unique<StridedGenerator>(
                    PhysAddr{get_uint64(gen["start"])}, PhysAddr{get_uint64(gen["stop"])}, get_uint64(gen["stride"]));
                generators.insert({name, std::move(g)});
            } else {
                auto g =
                    std::make_unique<StridedGenerator>(PhysAddr{get_uint64(gen["start"])}, get_uint64(gen["stride"]));
                generators.insert({name, std::move(g)});
            }
        } else if (type == "random") {
            uint64_t mask = maybe_get_uint64(gen, "mask", 0);
            uint64_t seed = maybe_get_uint64(gen, "seed", 0);

            auto distribution = gen["distribution"].get<std::string>();

            if (distribution == "uniform") {
                auto g = std::make_unique<UniformRand>(
                    PhysAddr{get_uint64(gen["start"])}, PhysAddr{get_uint64(gen["stop"])}, mask, seed);
                generators.insert({name, std::move(g)});
            } else {
                AMPERE_ERROR("unrecognized random distribution \"{}\" for generator \"{}\"", distribution, name);
            }
        } else if (type == "latmemrd") {
            uint64_t seed = maybe_get_uint64(gen, "seed", 0);
            int granule = maybe_get_uint64(gen, "granule", 8);
            auto g = std::make_unique<LatMemReadGenerator>(
                PhysAddr{get_uint64(gen["start"])}, PhysAddr{get_uint64(gen["stop"])}, granule, seed);
            generators.insert({name, std::move(g)});
        } else if (type == "list") {
            auto g = std::make_unique<ListGenerator>(get_addr_list(gen["address_list"]));
            generators.insert({name, std::move(g)});
        } else {
            AMPERE_ERROR("unrecognized generator type \"{}\" for generator \"{}\"", type, name);
        }

        if (gen.contains("region"))
            generators[name]->set_region(get_str(gen["region"]));
    }
}

uint64_t UopSourceProg::get_uint64(const json& jval)
{
    if (jval.is_string()) {
        std::string s = jval.get<std::string>();

        if (s.empty())
            AMPERE_FATAL("expected number, got empty string");

        if (s[0] == '@') {
            auto it = dynamic_parameters.find(s.substr(1));

            if (it == dynamic_parameters.end()) {
                AMPERE_FATAL("no such parameter \"{}\"", s.substr(1));
                return 0;
            } else {
                auto j = it->second.val();
                if (j.is_string())
                    return std::stol(j.get<std::string>(), nullptr, 0);
                else
                    return it->second.val().get<uint64_t>();
            }
        } else {
            return std::stol(jval.get<std::string>(), nullptr, 0);
        }
    } else {
        return jval.get<uint64_t>();
    }
}

std::string UopSourceProg::get_str(const json& jval)
{
    if (jval.is_string()) {
        std::string s = jval.get<std::string>();

        if (s.empty())
            AMPERE_FATAL("expected string, got empty string");

        if (s[0] == '@') {
            auto it = dynamic_parameters.find(s.substr(1));

            if (it == dynamic_parameters.end()) {
                AMPERE_FATAL("no such parameter \"{}\"", s.substr(1));
                return {};
            } else {
                auto j = it->second.val();
                if (j.is_string())
                    return j.get<std::string>();
                else
                    return it->second.val().get<std::string>();
            }
        } else {
            return jval.get<std::string>();
        }
    } else {
        return {};
    }
}

bool UopSourceProg::get_bool(const json& jval)
{
    if (jval.is_string()) {
        std::string s = jval.get<std::string>();

        if (s.empty())
            AMPERE_FATAL("expected number, got empty string");

        AMPERE_ASSERT(s == "true" || s == "false", "got incorrect string value");

        if (s == "true")
            return true;
    }
    return false;
}

std::vector<PhysAddr> UopSourceProg::get_addr_list(const json& jval)
{
    if (jval.is_string()) {
        std::string s = jval.get<std::string>();

        if (s.empty())
            AMPERE_FATAL("expected number, got empty string");

        if (s[0] == '@') {
            auto it = dynamic_parameters.find(s.substr(1));

            if (it == dynamic_parameters.end()) {
                AMPERE_FATAL("no such parameter \"{}\"", s.substr(1));
                return {};
            } else {
                return it->second.val().get<std::vector<PhysAddr>>();
            }
        } else {
            return jval.get<std::vector<PhysAddr>>();
        }
    } else {
        return {};
    }
}

uint64_t UopSourceProg::maybe_get_uint64(const json& jval, const char* field, uint64_t default_value)
{
    if (jval.count(field))
        return get_uint64(jval[field]);
    else
        return default_value;
}

bool UopSourceProg::maybe_get_bool(const json& jval, const char* field)
{
    if (jval.count(field))
        return get_bool(jval[field]);
    else
        return false;
}

void LatMemReadGenerator::gen_chase()
{
    size_t nr_elts = (stop - start) / granule;
    std::vector<uint32_t> perm = gen_random_permutation(nr_elts);
    std::vector<uint32_t> perm_inverse(nr_elts);

    for (size_t i = 0; i < nr_elts; ++i)
        perm_inverse[perm[i]] = i;

    arena.resize(nr_elts);

    for (size_t i = 0; i < nr_elts; ++i) {
        size_t next = perm_inverse[i] + 1;
        next = (next == nr_elts) ? 0 : next;
        arena[i] = start + perm[next] * granule;
    }
}

std::vector<uint32_t> LatMemReadGenerator::gen_random_permutation(size_t nr) const
{
    std::uniform_int_distribution<uint32_t> distribution{0, std::numeric_limits<uint32_t>::max()};
    std::default_random_engine engine;

    if (seed)
        engine.seed(seed);
    else
        engine.seed();

    auto rng_int = [&](size_t limit) -> uint32_t {
        return (limit + 1) * (distribution(engine) / (std::numeric_limits<uint32_t>::max() + 1.0));
    };

    std::vector<uint32_t> perm(nr);

    for (size_t i = 0; i < nr; ++i) {
        size_t t = rng_int(i);
        perm[i] = perm[t];
        perm[t] = i;
    }

    return perm;
}

} // namespace ampere::mesh
