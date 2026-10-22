//
// Copyright (c) 2019-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <boost/filesystem.hpp>
#include <elfio/elfio.hpp>
#include <fmt/format.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <chrono>
#include <algorithm>

#include "ampere/instruction_stream/instruction_stream.pb.h"
#include "ampere/protobuf_utils/protobuf_utils.h"
#include "ampere/archx/driver_is.h"
#include "ampere/archx/emulate.h"

// #define DEBUG_DRIVER
#if defined(DEBUG_DRIVER)
#define DBGDRV(format_str, ...) fmt::print(stderr, "DRVR: " format_str "\n", ##__VA_ARGS__)
#else
#define DBGDRV(format_str, ...) \
    do {                        \
    } while (0)
#endif

namespace ampere::archx {

namespace fs = boost::filesystem;

DriverIS::DriverIS(std::string instruction_stream_name,
                   uint64_t skip_instructions,
                   bool arg_speculative_memory_image,
                   bool arg_use_wrong_path_emulation,
                   std::uint64_t arg_validate_emulation,
                   std::string mem_image_path)
    : skip_instructions{skip_instructions},
      speculative_memory_image{arg_speculative_memory_image},
      use_wrong_path_emulation{arg_use_wrong_path_emulation},
      validate_emulation{arg_validate_emulation},
      mem_image_path{mem_image_path},
      instruction_stream_name{instruction_stream_name},
      instruction_stream(instruction_stream_name)
{
    InstructionIS::stream_features = instruction_stream.features();
    has_regs = InstructionIS::stream_features.all_destination_registers();
    has_flags = InstructionIS::stream_features.nzcv_flags_on_instructions();
    has_prefetch_regs = InstructionIS::stream_features.prefetch_registers();

    if (!has_regs && use_wrong_path_emulation) {
        fmt::print(stderr, "Trace has no registers: Disabling wrong path emulation\n");
        use_wrong_path_emulation = false;
        validate_emulation = false;
    }
}

DriverIS::DriverIS(std::string instruction_stream_name) : DriverIS(instruction_stream_name, 0, false, false, 0, "") {}

void DriverIS::initialize_first_instruction()
{
    assert(sequence_id == 0);

    InstructionIS bogus = InstructionIS();
    if (speculative_memory_image) {
        fmt::print("Speculative memory is enabled, searching for memory file...\n");

        // measure how long it takes to load and process
        unsigned duration(0);
        std::chrono::steady_clock::time_point timestamp[2];
        timestamp[0] = std::chrono::steady_clock::now();

        bogus.set_state(load_memory_image(instruction_stream_name));

        timestamp[1] = std::chrono::steady_clock::now();
        duration = std::chrono::duration_cast<std::chrono::seconds>(timestamp[1] - timestamp[0]).count();
        fmt::print("Speculative memory file loaded in {} seconds\n", duration);

        // Because there is no guarantee that read_instruction_stream() assigns
        // bogus' state to fetch_state, explicitly set it here to ensure the
        // memory behind the instructions doesn't get silently dropped.
        fetch_state = bogus.state();
    }

    next_instruction = read_instruction_stream(bogus);

    if (skip_instructions > 0) {
        // Skip instructions from the beginning of the instruction stream
        for (uint64_t i = 0; i < skip_instructions - 1; i++)
            next_instruction = read_instruction_stream(next_instruction);

        sequence_id = 0;
        next_instruction = read_instruction_stream(next_instruction);
    }

    /* sync fetch_state to arch_state before starting to execute to ensure the
     * initial state is correct. */
    arch_state = fetch_state;
}

ProgramCounter DriverIS::next_pc(bool* is_taken_branch)
{
    if (!instructions.contains(next_pc_id))
        read_next_instruction();
    if (is_taken_branch)
        *is_taken_branch = instructions.at(next_pc_id - 1)->is_taken_branch();
    return instructions.at(next_pc_id++)->program_counter();
}

Translation DriverIS::translate(VirtAddr va, bool is_instr = false)
{
    Translation tr(va,
                   Bitfield{fetch_state.read(TCR_EL1)},
                   Bitfield{fetch_state.read(TTBR0_EL1)},
                   Bitfield{fetch_state.read(TTBR1_EL1)},
                   is_instr);
    while (!tr.done()) {
        PhysAddr pte_addr = tr.next_pte();
        uint64_t pte_contents = fetch_state.read(pte_addr);
        tr.pte_fetched(pte_contents);
    }
    return tr;
}

Translation DriverIS::translate(ProgramCounter pc)
{
    return translate(VirtAddr{pc.value()}, /*is_instr=*/true);
}

std::shared_ptr<Instruction> DriverIS::fetch_wrong_path(ProgramCounter pc)
{
    DBGDRV("fetch wrong-path PC {}", pc);
    std::uint32_t itext = InstructionIS::DEFAULT_OPCODE;
    bool emulate = false; // only emulate if we found the real aarch64 instruction
    if (loaded_memory_image) {
        if (auto translation = translate(pc); !translation.faulted()) {
            auto pc_pa = translation.result_address();
            // read upper or lower 32b from a 64b value in arch_state
            std::uint32_t tmp_itext =
                pc_pa.extract_range(2, 2) ? arch_state.read(pc_pa - 4) >> 32u : arch_state.read(pc_pa) & 0xffffffffu;
            if (tmp_itext) { // assume nonzero is a valid instruction. Usually true
                DBGDRV("Read wrong path itext {:08x} from memory", tmp_itext);
                itext = tmp_itext;
                emulate = true;
            }
        }
    }

    // If we were already on wrong path, propagate the wrong path
    // outputs to the inputs (fetch_state) of the current instruction.

    ArchState wrong_path_fetch_state;
    inum_t prev_id;
    if (prev_wrong_path_instruction.has_value()) {
        // When we create an InstructionIS, we copy the input
        // arch_state to arch_state_output.  So even if no emulation
        // occurs, we have the correct state pass-though in
        // arch_state_output
        wrong_path_fetch_state = prev_wrong_path_instruction.value()->arch_state_output;
        prev_id = prev_wrong_path_instruction.value()->inum();
    } else if (prev_stream_instruction.has_value()) {
        // if we are just entering the wrong path, we must still apply
        // the outputs from the previous correct path
        // instruction. "prev_stream_instruction" is only defined on the
        // correct path.
        auto [state, updated] = read_dst_regs_from_trace(fetch_state, prev_stream_instruction.value());
        fetch_state = state;
        // do not clear the prev_stream_instruction as the state it contains
        // will be used to give data to the next correct-path instruction.

        // leave correct path fetch_state intact; it will be used after mispredict recovery.
        wrong_path_fetch_state = fetch_state;
        prev_id = prev_correct_path_id;
    } else {
        AMPERE_FATAL(
            "This should not happen. We are fetching wrong path. Which means there"
            " should be prior wrong path instruction or a correct path instruction."
            " If this assert fires, neither of those are true.");
    }

    // The constructor copies the supplied state to
    // arch_state_output. If we do not emulate, they'll stay the same.
    inum_t wrong_path_id(--wrong_path_sequence_id);

    prev_wrong_path_instruction =
        std::make_shared<InstructionIS>(pc, itext, wrong_path_id, prev_id, std::move(wrong_path_fetch_state));
    DBGDRV("\tmake wrong path instr with id {}, pc {} {}, prev_id {}",
           wrong_path_id.id(),
           pc,
           prev_wrong_path_instruction.value()->disasm(),
           prev_id.id());
    // we only try to emulate non-bogus instructions
    if (!emulate) {
        DBGDRV("Set artificial on id {}", wrong_path_id.id());
        prev_wrong_path_instruction.value()->_artificial_encoding = true;
    }

    wrong_path_instructions.emplace(prev_wrong_path_instruction.value()->inum().id(),
                                    prev_wrong_path_instruction.value());

    if (use_wrong_path_emulation && emulate) {
        // Update prev_wrong_path_instruction.arch_state_output with the output of this instruction.
        // Also calculate memory addresses referenced and the branch targets for any branches. For
        // branches, set next_pc for the branch mispredict handling

        // need to emulate before returning instruction to set up virtual addresses for memops
        emulate::emulate(prev_wrong_path_instruction.value().get());
    }
    return prev_wrong_path_instruction.value();
}

void DriverIS::call_emulation(InstructionIS& instr)
{
    // This is used for emulation validation for correct path
    // instructions and for emulating flags setters in traces that do
    // not have the flags values present.
    DBGDRV("call_emulation");
    // emulation writes arch_state_output
    if (validate_emulation) {
        DBGDRV("Emulating inum {}:  {}", instr.inum().id(), instr.disasm());
        emulate::emulate(&instr);
        if (instr.emulated()) {
            DBGDRV("\temulated!");
        }
    } else {
        // If we are not validating emulation, we still emulate some instructions:
        // 1) GPR prefetches because our traces don't have their VAs.
        // 2) flags writers in older traces.
        bool emulate = false;
        if (has_prefetch_regs && instr.is_gpr_prefetch()) {
            emulate = true;
        } else if (use_wrong_path_emulation && !has_flags && adis::may_write_flags(instr.adis_info())) {
            emulate = true;
        }
        if (emulate) {
            emulate::emulate_correct_path(&instr);
        }
    }
}

std::shared_ptr<Instruction> DriverIS::fetch(ProgramCounter pc)
{
    DBGDRV("\nDRVR: fetch from PC {}", pc);
    if (!on_wrong_path) {
        if (fetch_id >= sequence_id)
            read_next_instruction(); // adds one instruction to instructions map

        if (auto fetch_instruction = instructions.at(fetch_id); pc == fetch_instruction->program_counter()) {
            fetch_id++;
            prev_wrong_path_instruction.reset();
            prev_correct_path_id = fetch_instruction->inum();
            DBGDRV("\tfetch Returning inum {}", fetch_instruction->inum().id());
            return fetch_instruction;
        }

        on_wrong_path = true;
    }

    return fetch_wrong_path(pc);
}

void DriverIS::commit(std::shared_ptr<Instruction> instruction)
{
    // commit() is typically used for instructions that commit
    // architecturally.  However we also use it for instructions that
    // do not commit to advance the state of the trace in the
    // driver. An example of this latter case is a load that faults.
    DBGDRV("commit {}", instruction->inum().id());
    if (instructions.empty())
        throw std::runtime_error("Commit called with an empty instructions buffer.");
    const auto& begin = instructions.begin();
    if (instruction == begin->second) {
        std::shared_ptr<InstructionIS> instruction_is = begin->second;
        last_committed_id = instruction->inum().id();
        // apply the trace state outputs from this instruction to the the arch state
        if (instruction_is->emulated()) {
            // if emulated, update the arch_state_output set by emulation
            auto [state, _] =
                read_dst_regs_from_trace(instruction_is->arch_state_output, instruction_is->stream_instruction);
            arch_state = state;
        } else {
            // if not emulated, update the input state created by fetch
            auto [state, _] = read_dst_regs_from_trace(instruction->state(), instruction_is->stream_instruction);
            arch_state = state;
        }
        instructions.erase(begin);
    } else {
        constexpr auto msg = "Driver commit error expected instruction {} got {}";
        throw std::runtime_error(fmt::format(msg, begin->second->inum().id(), instruction->inum().id()));
    }
}

void DriverIS::read_next_instruction()
{
    DBGDRV("read_next_instruction");

    // adds InstructionIS to "instructions" map
    // sets next_instruction

    if (instructions.size() > 10000)
        throw std::runtime_error("Exceeded maximum number of uncommitted instructions");

    // Initialize first instruction if we have not yet done so
    if (sequence_id == 0)
        initialize_first_instruction();

    auto current_instruction = std::move(next_instruction);

    next_instruction = read_instruction_stream(current_instruction);

    current_instruction.set_next_pc(next_instruction.program_counter());
    auto instruction = std::make_shared<InstructionIS>(current_instruction);
    DBGDRV("read_next_instruction: inum: {} EL: {} PC: {:016x} DIS: {}",
           instruction->inum().id(),
           static_cast<unsigned>(instruction->state().exception_level()),
           instruction->pc.value(),
           instruction->disasm());
    instructions.emplace(instruction->inum().id(), instruction);
}

/* Check if event needs to update architectural state (memory, sysregs, etc.),
 * and record any needed updates. This requires that the previous instruction
 * be passed in because memory updates must be recorded for its state. */
void DriverIS::record_event(const is::Event& event, InstructionIS& prev)
{
    if (event.has_memory()) {
        const is::Memory& mem = event.memory();
        PhysAddr pa = PhysAddr(mem.address().pa());

        if (!prev.inum().initial_bogus_inst() && instruction_stream.version() <= 3) {
            /* Because memory updates for translation tables are immediately before
             * the instruction which needs them in the instruction stream, and will
             * therefore only be observed *after* the instruction which needs them
             * commits, add all memory writes to the state for the preceding
             * instruction, too. */
            prev.set_state(std::move(prev.state()).write(pa, (uint8_t*)mem.value().data(), mem.value().size()));
        }
        fetch_state = std::move(fetch_state).write(pa, (uint8_t*)mem.value().data(), mem.value().size());
    } else if (event.has_sysreg()) {
        const is::SysReg& s = event.sysreg();
        SysReg sysreg = SysReg(s.op0(), s.op1(), s.crn(), s.crm(), s.op2());
        fetch_state = std::move(fetch_state).write(sysreg, s.value());
    } else if (event.has_context()) {
        const is::Context& c = event.context();
        if (c.type() == is::Context::EL) {
            // we must allow version 3 traces to read the EL "after" the
            // instruction
            if (!prev.inum().initial_bogus_inst() && instruction_stream.version() <= 3) {
                prev.set_state(std::move(prev.state()).set_exception_level(static_cast<EL>(c.value())));
            }
            fetch_state = std::move(fetch_state).set_exception_level(static_cast<EL>(c.value()));
        }
    }
}

std::pair<ArchState, bool> DriverIS::read_dst_regs_from_trace(ArchState state, const is::Instruction& strm_inst)
{
    DBGDRV("read_dst_regs_from_trace. PC {:016x} ITXT {:08x}", strm_inst.program_counter().va(), strm_inst.opcode());
    // returned pair is (state, mutated-was-mutated)
    bool mutated = false;
    // set bool to true if we change anything
    for (int i = 0; i < strm_inst.dst_register_size(); ++i) {
        LReg regno = cvt_gpr(strm_inst.dst_register(i).number(), archx::LReg::SP);
        state = std::move(state).write(regno, strm_inst.dst_register(i).value());
        mutated = true;
    }
    for (int i = 0; i < strm_inst.dst_vfp_register_size(); ++i) {
        LReg regno = cvt_vec(strm_inst.dst_vfp_register(i).number());
        std::array<uint64_t, 2> vecval{strm_inst.dst_vfp_register(i).ls_value(),
                                       strm_inst.dst_vfp_register(i).ms_value()};
        state = std::move(state).write_vfp(regno, vecval);
        mutated = true;
    }
    for (int i = 0; i < strm_inst.dst_pred_register_size(); i++) {
        LReg regno = cvt_pred(strm_inst.dst_pred_register(i).number());
        state = std::move(state).write_pred(regno, strm_inst.dst_pred_register(i).value());
        mutated = true;
    }
    if (strm_inst.has_dst_nzcv()) {
        const unsigned nzcv = strm_inst.dst_nzcv(); // upper nibble set
        state = std::move(state).write(NZCV, nzcv);
        mutated = true;
    }
    return std::make_pair(state, mutated);
}

void DriverIS::read_src_regs_from_trace(const is::Instruction& strm_inst)
{
    for (int i = 0; i < strm_inst.register__size(); ++i) {
        const std::uint32_t trc_regno = strm_inst.register_(i).number();
        const LReg regno = cvt_gpr(trc_regno, archx::LReg::SP);
        const std::uint64_t val = strm_inst.register_(i).value();
        fetch_state = std::move(fetch_state).write(regno, val);
        if (validate_emulation >= 3) {
            fmt::print(stderr, "Loading {} = {:016x}\n", archx::regstr(regno), val);
        }
    }
    for (int i = 0; i < strm_inst.vfp_register_size(); ++i) {
        LReg regno = cvt_vec(strm_inst.vfp_register(i).number());
        std::array<uint64_t, 2> vecval{strm_inst.vfp_register(i).ls_value(), strm_inst.vfp_register(i).ms_value()};
        fetch_state = std::move(fetch_state).write_vfp(regno, vecval);
        if (validate_emulation >= 3) {
            fmt::print(stderr, "Loading {} = {:016x}_{:016x}\n", archx::regstr(regno), vecval[1], vecval[0]);
        }
    }
    for (int i = 0; i < strm_inst.src_pred_register_size(); i++) {
        LReg regno = cvt_pred(strm_inst.src_pred_register(i).number());
        fetch_state = std::move(fetch_state).write_pred(regno, strm_inst.src_pred_register(i).value());
        if (validate_emulation >= 3) {
            fmt::print(stderr, "Loading {} = {:04x}\n", archx::regstr(regno), strm_inst.src_pred_register(i).value());
        }
    }
    if (strm_inst.has_src_nzcv()) {
        const std::uint32_t nzcv = strm_inst.src_nzcv(); // upper nibble set
        fetch_state = std::move(fetch_state).write(NZCV, nzcv);
        if (validate_emulation >= 3) {
            fmt::print(stderr, "Loading NZCV = {}\n", archx::nzcv_str(nzcv));
        }
    }
}

InstructionIS DriverIS::read_instruction_stream(InstructionIS& prev)
{
    DBGDRV("read_instruction_stream prev inum {}:  {}", prev.inum().id(), prev.disasm());
    is::Event event;
    bool status = instruction_stream.read(event);
    std::optional<SimulationControl> simulation_control;
    while (status && !event.has_instruction()) {
        if (event.has_control()) {
            switch (event.control().type()) {
            case is::Control::START_MEASUREMENT:
                simulation_control = SimulationControl::START_MEASUREMENT;
                break;
            case is::Control::STOP_MEASUREMENT:
                simulation_control = SimulationControl::STOP_MEASUREMENT;
                break;
            default:
                throw std::runtime_error("Unknown is::Control value");
            }
        }
        record_event(event, prev); // updates fetch_state
        event.Clear();
        status = instruction_stream.read(event);
    }
    if (!status)
        throw std::runtime_error("No instructions remaining in stream");

    if (!prev.inum().initial_bogus_inst()) {
        // Do not emulate the very first bogus/fake instruction. The
        // page tables are not loaded yet. Translation will fail
        // trying to get the itext for the instruction.
        call_emulation(prev);
    } else {
        DBGDRV("\tNot emulating very first instruction");
    }

    if (prev.emulated()) {
        // sometimes we emulate correct path instructions to set missing PSTATE.NZCV flags
        DBGDRV("\tprev emulated so using its output state as next fetch state");
        fetch_state = prev.arch_state_output;
    } else {
        DBGDRV("\tprev NOT emulated. inum {}", prev.inum().id());
    }

    if (prev_stream_instruction.has_value()) {
        DBGDRV("\tprev stream instr has value");
        if (validate_emulation) {
            if (prev.emulated()) {
                validate_one_emulation(prev_stream_instruction.value(), prev);
            } else if (validate_emulation >= 3) {
                dump_trace_updates_for_debugging(prev_stream_instruction.value(), prev);
            }
        }

        // version <= 5 traces, had input state on dst regs of
        // inum==1, so we read it before to setup for inum=1. We don't
        // want to clobber emulated outputs of inum=1.
        if (prev.inum().id() != 1 || instruction_stream.version() > 5) {
            auto [state, updated] = read_dst_regs_from_trace(fetch_state, prev_stream_instruction.value());
            fetch_state = state;
        } else {
            DBGDRV("\tskipping dst read for inum 1 for older traces");
        }
    }

    is::Instruction stream_instruction = event.instruction();
    prev_stream_instruction = stream_instruction;
    read_src_regs_from_trace(stream_instruction);
    if (sequence_id == 0 && instruction_stream.version() <= 5) {
        // In version <= 5 traces, the tracer erroneously attaches the
        // initial state for all the registers to the dest registers
        // on the first instruction. So we must read the dest regs to
        // get the initial state for all the registers.
        DBGDRV("\t*** Reading initial state from first real trace instr!");
        auto [state, updated] = read_dst_regs_from_trace(fetch_state, stream_instruction);
        fetch_state = state;
    }

// If this instruction is one of our custom semihosting instructions which
// we use to communicate information from workloads to the tracing
// infrastructure, convert this instruction to a NOP. We are forced to
// convert these instructions to NOPs when creating ELF files for perf
// snippets, so doing this here maintains parity with RTL simulations.
#define IS_HLT(opcode, imm) (opcode == ((imm << 5) | 0xd4400000))
#define PERF_SEMIHOSTING_NUM 0xf739  /* perf_semihosting.h */
#define TRACE_SEMIHOSTING_NUM 0x38ce /* trace_semihosting.h */
    if (IS_HLT(stream_instruction.opcode(), PERF_SEMIHOSTING_NUM)
        || IS_HLT(stream_instruction.opcode(), TRACE_SEMIHOSTING_NUM))
    {
        stream_instruction.set_opcode(0xd503201f); /* NOP */
    }

    inum_t prev_id(sequence_id);
    inum_t id(++sequence_id);
    DBGDRV("create correct path instr pc={:x} id={} prev_id={}",
           stream_instruction.program_counter().va(),
           id.id(),
           prev_id.id());
    auto inst = InstructionIS(stream_instruction, id, prev_id, fetch_state, simulation_control);
    return inst;
}

void DriverIS::recover(inum_t id)
{
    // For mispredicted branch, we send in the id of the branch
    // because it completes and recover() should find or create (if
    // wrong path) the next instruction on that path.
    //
    // RAW_SCRUB and CSF_NO_CMT are different; they send in
    // prev_id(). RAW_SCRUB sends in prev_id() because we need to redo
    // the load that suffered the RAW_SCRUB.  CSF_NO_CMT (like self
    // synchronizing sysreg writes) wait until they are oldest in ROB,
    // then flush everything younger, then rexecute themselves.

    DBGDRV("recover from {}", id.id());

    if (id.wrong_path()) {
        recover_wrong_path(id);
    } else {
        recover_using_trace(id);
    }
}
void DriverIS::recover_wrong_path(inum_t id)
{
    DBGDRV("\trecover_wrong_path id={}", id.id());

    // need to find the output arch state of the recovery instruction
    // to use to restart wrong path fetching.
    if (auto search = wrong_path_instructions.find(id.id()); search != wrong_path_instructions.end()) {
        DBGDRV("Found wrong path state for recovery from {}", id.id());
        prev_wrong_path_instruction = search->second;
    }

    // Clean out everything younger than (and including) the thing
    // that caused this recovery (the mispredicted
    // branch/RAW_SCRUB/CSF_NO_CMT). These inum values will be younger
    // (more negative) than the event's inum.  The
    // wrong_path_sequence_id is the value of the last assigned wrong
    // path instruction.
    DBGDRV("Clearing wrong path from {} to {}", id.id(), wrong_path_sequence_id);
    for (std::int64_t winum = id.id(); winum >= wrong_path_sequence_id; --winum) {
        wrong_path_instructions.erase(winum);
    }
}

void DriverIS::recover_using_trace(inum_t id)
{
    DBGDRV("\trecover_using_trace id={}, next_instruction={}", id.id(), next_instruction.inum().id());
    inum_t next_id(id.id() + 1);

    if (next_id == next_instruction.inum())
        read_next_instruction();

    if (instructions.contains(next_id.id())) {
        fetch_id = next_id.id();
        next_pc_id = next_id.id();
        on_wrong_path = false;
        prev_correct_path_id = next_id;
        DBGDRV("\tNext correct path instr is inum {}", next_id.id());
        wrong_path_instructions.clear();
    } else {
        auto msg = fmt::format("Instruction ID {} does not exist in DriverIS", next_id.id());
        throw std::runtime_error(msg);
    }
}

uint64_t DriverIS::read_memory(const PhysAddr addr) const
{
    return arch_state.read(addr);
}

uint64_t DriverIS::read_sysreg(const SysReg sysreg) const
{
    return arch_state.read(sysreg);
}

EL DriverIS::exception_level() const
{
    return arch_state.exception_level();
}

uint16_t DriverIS::asid() const
{
    Bitfield tcr_el1 = Bitfield{read_sysreg(TCR_EL1)};

    /* TCR_EL1.A1 selects whether the ASID resides in TTBR0_EL1 or TTBR1_EL1 */
    if (tcr_el1.extract_range(22, 22)) {
        return (uint16_t)Bitfield{read_sysreg(TTBR1_EL1)}.extract_range(63, 48);
    } else {
        return (uint16_t)Bitfield{read_sysreg(TTBR0_EL1)}.extract_range(63, 48);
    }
}

uint64_t DriverIS::committed_id() const
{
    return last_committed_id;
}

static fs::path remove_extensions(const fs::path& src)
{
    fs::path out{src};
    if (out.has_extension()) {
        while (out.extension() != ".is") // remove the .xz if present
            out = out.stem();
        if (out.has_extension()) // remove the .is
            out = out.stem();
    }
    return out;
}

ArchState DriverIS::load_memory_image(const std::string& instruction_stream_name)
{
    // Look for a memory image file alongside an instruction stream; if it exists, load it into the fetch_state.
    fs::path p{instruction_stream_name};
    fs::path directory = p.parent_path();

    p = remove_extensions(p);
    p = directory / p;
    fs::path mem_path = p;
    fs::path elf_path = p;
    mem_path += ".mem";
    elf_path += ".elf";

    if (mem_image_path.size() > 0) {
        const fs::path mem_knob{mem_image_path};
        fs::path mem_path;
        if (mem_knob.has_extension() && mem_knob.extension() == ".mem") {
            // use the knob directly as a full path if it ends in a .mem
            mem_path = mem_knob;
        } else {
            // build the path to the .mem file from the instruction stream file name
            const fs::path is_path{instruction_stream_name};
            mem_path = mem_knob / remove_extensions(is_path);
            mem_path += ".mem";
        }
        if (fs::exists(mem_path)) {
            fmt::print("Loaded memory image cache file {}\n", mem_path.native());
            std::ifstream mem_file{mem_path.native()};
            loaded_memory_image = true;
            return fetch_state.load_memory(mem_file);
        } else {
            fmt::print("Did not find cache file {}\n", mem_path.native());
        }
    }
    if (fs::exists(mem_path)) {
        fmt::print("Loaded memory image file {}\n", mem_path.native());

        std::ifstream mem_file{mem_path.native()};
        loaded_memory_image = true;
        return fetch_state.load_memory(mem_file);
    } else if (fs::exists(elf_path)) {
        ELFIO::elfio reader;
        if (!reader.load(elf_path.native())) {
            throw std::runtime_error("ELFIO failed to load ELF\n");
        }
        fmt::print("Loaded memory image from ELF file {}\n", elf_path.native());
        loaded_memory_image = true;
        return fetch_state.load_elf(reader);

    } else {
        fmt::print("Did not find memory image file {} or ELF {}. Falling back to building from instruction stream.\n",
                   mem_path.native(),
                   elf_path.native());

        is::InstructionStreamReader reader(instruction_stream_name);
        is::Instruction stream_instruction;
        while (reader.read(stream_instruction)) {
            PhysAddr pc_pa{stream_instruction.program_counter().pa()};
            uint32_t opcode = stream_instruction.opcode();
            if (fetch_state.read(pc_pa, 4 /*bytes*/) == 0)
                fetch_state = std::move(fetch_state).write(pc_pa, opcode);
        }
        loaded_memory_image = true;
    }

    return fetch_state;
}

void DriverIS::validate_emulation_memops(const is::Instruction& strm_inst, const InstructionIS& sim_inst)
{
    const size_t sn = strm_inst.memop_size();
    const size_t en = sim_inst.emulated_virt_addr.size();
    const VirtAddr bad(0xDEADBEEF);
    for (size_t i = 0; i < std::max(sn, en); ++i) {
        const auto sva = (i < sn) ? VirtAddr(strm_inst.memop(i).va()) : bad;
        const auto eva = (i < en) ? sim_inst.emulated_virt_addr[i] : bad;
        if (sva != eva) {
            const std::string eq = (sva == eva) ? "==" : "!=";
            const std::uint64_t level = (sva == eva) ? 4 : 3;
            if (validate_emulation >= level) {
                fmt::print(stderr,
                           "inum: {} memref {}, TRC: {:016x} {} EMU: {:016x},  sn {}, en{}\n",
                           sim_inst.inum().id(),
                           i,
                           sva.value(),
                           eq,
                           eva.value(),
                           sn,
                           en);
            }
        }
    }
}

void DriverIS::validate_emulation_dest_regs(const is::Instruction& strm_inst, const InstructionIS& sim_inst)
{
    auto mod_regs = sim_inst.modified_regs;
    for (int i = 0; i < strm_inst.dst_register_size(); ++i) {
        LReg regno = cvt_gpr(strm_inst.dst_register(i).number(), archx::LReg::SP);
        bool sim_missing;
        if (auto itr = mod_regs.find(regno); itr != mod_regs.end()) {
            mod_regs.erase(itr);
            sim_missing = false;
        } else {
            sim_missing = true;
        }
        const std::uint64_t sval = strm_inst.dst_register(i).value();
        const std::uint64_t eval = sim_inst.arch_state_output.read(regno);
        {
            const char* category = "matched";
            const char* eq = "==";
            std::uint64_t level = 3;
            if (sim_missing) {
                category = "emu-missing";
                eq = "!=";
                level = 2;
            } else if (sval != eval) {
                category = "emu-diverged";
                eq = "!=";
                ++emulation_divergences;
                level = 2;
            }
            if (validate_emulation >= level) {
                fmt::print(stderr,
                           "inum: {} REG {}: trc: {:016x} {} emu: {:016x} {}\n",
                           sim_inst.inum().id(),
                           archx::regstr(regno),
                           sval,
                           eq,
                           eval,
                           category);
            }
        }
    }

    for (auto regno : mod_regs) {
        if (regno == archx::LReg::FLAGS) {
            const std::uint64_t nzcv = sim_inst.arch_state_output.read(NZCV);
            if (strm_inst.has_dst_nzcv()) {
                const std::uint32_t strm_nzcv = strm_inst.dst_nzcv();
                if (strm_nzcv != nzcv) {
                    ++emulation_divergences;
                    if (validate_emulation >= 2) {
                        fmt::print(stderr,
                                   "inum: {} REG NZCV: trc: {:x} {} != emu: {:x} {} emu-diverged\n",
                                   sim_inst.inum().id(),
                                   strm_nzcv >> 28,
                                   archx::nzcv_str(strm_nzcv),
                                   nzcv >> 28,
                                   archx::nzcv_str(nzcv));
                    }
                } else {
                    if (validate_emulation >= 4) {
                        fmt::print(stderr,
                                   "inum: {} REG NZCV: trc: {:x} {} == emu: {:x} {} matched\n",
                                   sim_inst.inum().id(),
                                   strm_nzcv >> 28,
                                   archx::nzcv_str(strm_nzcv),
                                   nzcv >> 28,
                                   archx::nzcv_str(nzcv));
                    }
                }
            } else if (validate_emulation >= 3) {
                fmt::print(stderr,
                           "inum: {} REG NZCV: trc: NA != emu: {:x} {} trc-missing\n",
                           sim_inst.inum().id(),
                           nzcv >> 28,
                           archx::nzcv_str(nzcv));
            }
        } else {
            const std::uint64_t eval = sim_inst.arch_state_output.read(regno);
            const std::uint64_t sval = sim_inst.state_ref().read(regno);
            if (sval == eval) {
                if (validate_emulation >= 4) {
                    fmt::print(stderr,
                               "inum: {} REG {}: trc: {:016x} == emu: {:016x} trc-unchanged\n",
                               sim_inst.inum().id(),
                               archx::regstr(regno),
                               sval,
                               eval);
                }
            } else {
                if (validate_emulation >= 3) {
                    fmt::print(stderr,
                               "inum: {} REG {}: trc: NA != emu: {:016x} trc-missing\n",
                               sim_inst.inum().id(),
                               archx::regstr(regno),
                               eval);
                }
            }
        }
    }
}
void DriverIS::dump_trace_updates_for_debugging(const is::Instruction& strm_inst, const InstructionIS& sim_inst) const
{
    fmt::print(stderr,
               "Dumping for inum {}: {} {:08x} {}\n",
               sim_inst.inum().id(),
               adis::encoding_name(sim_inst.adis_info()),
               sim_inst.opcode(),
               sim_inst.disasm());
    for (int i = 0; i < strm_inst.dst_register_size(); ++i) {
        const LReg regno = cvt_gpr(strm_inst.dst_register(i).number(), archx::LReg::SP);
        const std::uint64_t sval = strm_inst.dst_register(i).value();
        fmt::print(
            stderr, "inum: {} REG {}: trc: {:016x} not-emulated\n", sim_inst.inum().id(), archx::regstr(regno), sval);
    }
}

void DriverIS::validate_one_emulation(const is::Instruction& strm_inst, const InstructionIS& sim_inst)
{
    if (validate_emulation >= 2) {
        fmt::print(stderr,
                   "Validating inum {}: {} PC: {:016x}, ITXT {:08x} {}\n",
                   sim_inst.inum().id(),
                   adis::encoding_name(sim_inst.adis_info()),
                   sim_inst.program_counter().value(),
                   sim_inst.opcode(),
                   sim_inst.disasm());
    }
    validate_emulation_memops(strm_inst, sim_inst);
    validate_emulation_dest_regs(strm_inst, sim_inst);

    if (false) { // debugging
        const auto base_reg = archx::LReg::SP;
        const auto regval = sim_inst.arch_state_output.read(base_reg);
        fmt::print(stderr, "validate_one_emulation: {} = {:016x}\n", archx::regstr(base_reg), regval);
    }
}
uint64_t DriverIS::validation(const std::string& key [[maybe_unused]]) const
{
    if (key == "emulation divergences") {
        return emulation_divergences;
    }
    return 0;
}

} // namespace ampere::archx

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
