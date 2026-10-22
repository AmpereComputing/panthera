//
// Copyright (c) 2018-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_TRACING_CONTEXT_H
#define AMPERE_TRACING_CONTEXT_H

#include <assert.h>
#include <optional>
#include <stdint.h>

#include <nlohmann/json.hpp>

extern "C" {
#include <qemu-plugin.h>
}

#include "ampere/archx/sysreg.h"

namespace ampere {
namespace tracer {

using namespace ampere::archx;
using nlohmann::json;

enum ContextUpdateType {
    TRACING,    /* This update is due to the tracing infrastructure
                   initializing/restoring state, not due to the code being
                   executed */
    CONTEXTIDR, /* This update is due to CONTEXTIDR_EL1 being written */
    ERET,       /* This update is due to an exception return to a lower EL */
    SYNC_EXC,   /* This update is due to a synchronous exception (i.e. syscall,
                   memory fault) */
    ASYNC_EXC,  /* This update is due to a non-synchronous exception (IRQ, FIQ,
                   SError) */
};
typedef void (*context_update_cb)(uint64_t el, uint64_t pid, ContextUpdateType update_type);
typedef void (*sysreg_cb)(SysReg sysreg, uint64_t value);

/* Track the current PID and EL */
class Context
{
public:
    Context(qemu_plugin_id_t id,
            qemu_plugin_simple_cb_t reset_callback,
            context_update_cb update_callback,
            uint64_t initial_el,
            uint64_t initial_pid)
        : vbar_el1(0xdeadbeefdeadbeef),
          vbar_el2(0xdeadbeefdeadbeef),
          vbar_el3(0xdeadbeefdeadbeef),
          first_instruction(true),
          last_insn_syscall(true),
          last_insn_eret(true),
          prev_mrs_encoding(std::nullopt),
          current_el(initial_el),
          current_pid(initial_pid),
          plugin_id(id),
          reset_callback(reset_callback),
          update_callback(update_callback),
          msr_callback(nullptr),
          mrs_callback(nullptr)
    {
        assert(reset_callback);
        assert(update_callback);
    }

    /* The next two functions need to be called from the plugin's
     * plugin_needs_before_insn and plugin_before_insn functions */
    bool needs_before_insn(qemu_plugin_id_t id, uint64_t pc, uint32_t encoding);
    void before_insn(unsigned int cpu_index, uint64_t pc, uint32_t encoding);

    /* Is the PC the first instruction in a recognized exception vector? */
    bool is_exception(uint64_t pc);

    /* Is the PC the first instruction inside the synchronous region of an
     * exception vector? */
    bool is_synchronous_exception(uint64_t pc);

    /* Set the callback to be called on MRS/MSR instructions. These callbacks
     * report the values written to or read from translation or counter timer
     * sysregs and are typically only needed when actively tracing regions of
     * execution to be later simulated */
    void set_sysreg_callbacks(sysreg_cb msr_callback, sysreg_cb mrs_callback);

    /* Serialize Context to JSON or initialize internal state from it. Intended
     * to be called to save/restore plugin state at the same time checkpoints
     * are saved/restored. */
    void to_json(json& jval);
    bool from_json(const json& jval);

private:
    /* Current vector base addresses, for tracking when EL might be changing */
    uint64_t vbar_el1;
    uint64_t vbar_el2;
    uint64_t vbar_el3;

    /* If either of the following three variables is set, PSTATE is
     * uninitialized or was likely updated by the current instruction. Re-read
     * it to determine EL when we see the next instruction */
    bool first_instruction;
    bool last_insn_syscall;
    bool last_insn_eret;

    /* Set to the encoding */
    std::optional<uint32_t> prev_mrs_encoding;

    /* The main context reported to context_update_cb */
    uint64_t current_el;
    uint64_t current_pid;

    /* QEMU plugin id */
    qemu_plugin_id_t plugin_id;

    qemu_plugin_simple_cb_t reset_callback;
    context_update_cb update_callback;
    sysreg_cb msr_callback;
    sysreg_cb mrs_callback;
};

uint64_t num_string(const json& jval);

} // namespace tracer
} // namespace ampere

#endif /* AMPERE_TRACING_CONTEXT_H */

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
