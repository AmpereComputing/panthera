//
// Copyright (c) 2019-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <inttypes.h>
#include <map>
#include <optional>
#include <vector>
#include "ampere/adis/src/adis.h"

extern "C" {
#include <qemu-plugin.h>
}

#include "ampere/archx/el.h"
#include "ampere/archx/sysreg.h"
#include "ampere/archx/addr.h"

#include "common.h"
#include "context.h"

namespace ampere::tracer {

using namespace archx;

struct MemOp
{
    MemOp(uint64_t va, uint64_t pa, size_t bytes, bool is_store, bool is_io, bool is_predicated_off = false)
        : va{va}, pa{pa}, bytes{bytes}, is_store{is_store}, is_io{is_io}, predicated_off(is_predicated_off)
    {
        if (bytes > 128)
            throw std::runtime_error("Attempting to create MemOp with size > 128 bits");
    }
    MemOp(uint64_t va, size_t bytes, bool is_store) : va{va}, bytes{bytes}, is_store{is_store}, is_io{false}
    {
        if (bytes > 128)
            throw std::runtime_error("Attempting to create MemOp with size > 128 bits");
        pa.reset();
    }
    VirtAddr va;
    std::optional<PhysAddr> pa = std::nullopt;
    size_t bytes; /* width of access in bytes */
    bool is_store;
    bool is_io;
    bool predicated_off;
    // Although not initialized in the constructor, it should be expected that
    // `value` is initialized if `pa` is set and is_io is false, and any
    // tracing code should therefore ensure the first `bytes` bytes of `value`
    // are initialized before passing this struct through to any of the
    // individual tracers.
    uint8_t value[128];
};

struct Instruction
{
    Instruction(uint64_t pc, uint64_t pa, uint32_t opcode, std::vector<MemOp> memops = {})
        : pc{pc}, pa{pa}, opcode{opcode}, memops{memops}
    {
    }
    Instruction(uint64_t pc, uint32_t opcode, std::vector<MemOp> memops = {}) : pc{pc}, opcode{opcode}, memops{memops}
    {
        pa.reset();
    }
    ProgramCounter pc;
    std::optional<PhysAddr> pa = std::nullopt;
    uint32_t opcode;
    std::vector<MemOp> memops;
    gpr_map_t source_regs;     // not always full source registers... currently only valid for TLBI/divide instructions
    vec_map_t source_vfp_regs; // not always full source registers... currently only valid for divide/sqrt instructions
    pred_map_t source_pred_regs;
    std::optional<uint32_t> exception_class; // 0b100100 is 'Data Abort taken without a change in Exception level', the
                                             // only one we currently support
    adis::encoding_enum_t adis_info{adis::ENC_INVALID}; // ADIS encoding enum, from decoding, for ADIS API dispatch
};

class Tracer
{
public:
    enum struct Type : uint8_t {
        BS,
        ELF,
        IS,
        MS
    };

    static const char* type_str(Type type)
    {
        switch (type) {
        case Type::BS:
            return "BS";
        case Type::ELF:
            return "ELF";
        case Type::IS:
            return "IS";
        case Type::MS:
            return "MS";
        default:
            break;
        }
        return "??";
    }

    /* Factory method to create a tracer object of the correct type. The
     * created subclass of Tracer will immediately begin receiving traced
     * instructions consistent with the state provided here. */
    static std::unique_ptr<Tracer> make_tracer(const Type type,
                                               const std::string filename_base,
                                               StateAccessor* stateaccessor,
                                               bool flat_map,
                                               uint64_t elf_entry_point,
                                               bool debug,
                                               bool record_destination_registers,
                                               bool sve_mode,
                                               uint32_t dcache_size_bytes, /* dcache size to filter mem stream through*/
                                               uint32_t icache_size_bytes, /* icache size to filter mem stream through*/
                                               uint64_t elf_section_span);

    /* Signify when the "interesting" portion of the trace begins and ends */
    virtual bool start_measurement() = 0;
    virtual bool stop_measurement() = 0;

    /* Tells the tracer to finish the tracing session and write any final
     * output. This will always be called before destruction. */
    virtual bool stop_tracing(bool sve_observed) = 0;

    /* Tells the tracer to stop tracing and delete any files it has written to
     * disk. This is most frequently used when one tracer encounters an error
     * and can no longer continue tracing. */
    virtual void abort_tracing() = 0;

    /* Called every time a sysreg is written between calls to start_tracing and
     * stop_tracing. Called after the call to instruction_exec for the
     * instruction causing the sysreg to be written, but before the subsequent
     * instruction. */
    virtual bool sysreg_write(const archx::SysReg sysreg, uint64_t value) = 0;

    /* Called every time a relevant sysreg is read between calls to
     * start_tracing and stop_tracing. Called after the call to
     * instruction_exec for the instruction causing the sysreg to be read,
     * but before the subsequent instruction. */
    virtual bool sysreg_read(const archx::SysReg sysreg, uint64_t value) = 0;

    /* Called every time the EL changes between calls to start_tracing and
     * stop_tracing. Called after the call to instruction_exec for the
     * instruction causing the context change, but before the subsequent
     * instruction. */
    virtual bool el_change(const archx::EL el, ContextUpdateType update_type) = 0;

    /* Called every time an instruction is executed. This is called immediately
     * after the instruction is executed in QEMU (memory contents and register
     * values exposed via StateAccessor will reflect that the instruction has
     * been executed). next_pc contains the program counter of the instruction
     * which will follow this one. */
    virtual bool instruction_exec(const Instruction& instruction, const ProgramCounter next_pc) = 0;

    /* called just the tracer is created. It allows for capturing the
       state before the first traced instruction. This is primarily
       for the IStracer; others do not care do we supply a default
       implementation */
    virtual void record_initial_state() {};
    virtual ~Tracer() = default;
};

// return a pair: bool indicating success or failure and an
// optional va when we handle the prefetch address generation.
// (Some prefetches, like SVE prefetches are handled elsewhere, like normal memops.)
std::pair<bool, std::optional<archx::VirtAddr>> prefetch_va(const std::string& tracer_name,
                                                            const Instruction& instruction,
                                                            const adis::encoding_enum_t adis_info);

} // namespace ampere::tracer

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
