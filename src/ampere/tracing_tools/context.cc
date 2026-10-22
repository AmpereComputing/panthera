//
// Copyright (c) 2018-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "common.h"
#include "context.h"
#include "encodings.h"

extern "C" {
#include <qemu-plugin.h>
}

namespace ampere {
namespace tracer {

#define TRACING_CONTEXT_LATEST_VERSION 8

bool Context::needs_before_insn(qemu_plugin_id_t id, uint64_t pc, uint32_t encoding)
{
    if (IS_MSR(encoding)) {
        if (IS_MSR_REG(encoding, CONTEXTIDR_EL1) || IS_MSR_REG(encoding, VBAR_EL1) || IS_MSR_REG(encoding, VBAR_EL2)
            || IS_MSR_REG(encoding, VBAR_EL3)
            || (msr_callback
                && (IS_MSR_REG(encoding, SCTLR_EL1) || IS_MSR_REG(encoding, TCR_EL1) || IS_MSR_REG(encoding, TTBR0_EL1)
                    || IS_MSR_REG(encoding, TTBR1_EL1))))
        {
            //// The commented-out sysregs below are not needed at this time because we do
            //// not support tracing at or above EL2
            //                IS_MSR_REG(encoding, SCTLR_EL2) ||
            //                IS_MSR_REG(encoding, SCTLR_EL3) ||
            //                IS_MSR_REG(encoding, HCR_EL2) ||
            //                IS_MSR_REG(encoding, SCR_EL3) ||
            //                IS_MSR_REG(encoding, TCR_EL2) ||
            //                IS_MSR_REG(encoding, TCR_EL3) ||
            //                IS_MSR_REG(encoding, VTCR_EL2) ||
            //                IS_MSR_REG(encoding, TTBR0_EL2) ||
            //                IS_MSR_REG(encoding, TTBR0_EL3) ||
            //                IS_MSR_REG(encoding, TTBR1_EL2) ||
            //                IS_MSR_REG(encoding, VTTBR_EL2) ||
            return true;
        }
    } else if (mrs_callback && IS_MRS(encoding)
               && (IS_MRS_REG(encoding, CNTVCT_EL0) || IS_MRS_REG(encoding, CNTPCT_EL0)))
    {
        return true;
    }

    return IS_ERET(encoding) || IS_SVC(encoding) || IS_HVC(encoding) || IS_SMC(encoding) || is_exception(pc)
           || first_instruction || last_insn_eret || last_insn_syscall || prev_mrs_encoding;
}

bool Context::is_exception(uint64_t pc)
{
    // Mask off PC 10:7, which is is the exception vector's offset within the
    // vector table (PC 6:0 is the offset *within* a particular vector, and we
    // only care about catching the initial).
    uint64_t vbar_masked_pc = pc & 0xfffffffffffff87f;

    return vbar_masked_pc == vbar_el1 || vbar_masked_pc == vbar_el2 || vbar_masked_pc == vbar_el3;
}

inline bool Context::is_synchronous_exception(uint64_t pc)
{
    /*
     * Exception vector offsets from VBAR_ELx:
     *
     * | Exception taken from                      |               Exception types:                     |
     * |                                           | Synchronous | IRQ/vIRQ | FIQ/vFIQ | SError/vSError |
     * | Current EL w/ SP_EL0                      | 0x000       | 0x080    | 0x100    | 0x180          |
     * | Current EL w/ SP_ELx, x>0                 | 0x200       | 0x280    | 0x300    | 0x380          |
     * | Lower EL, immediately lower level AArch64 | 0x400       | 0x480    | 0x500    | 0x580          |
     * | Lower EL, immediately lower level AArch32 | 0x600       | 0x680    | 0x700    | 0x780          |
     *
     * From this table, we can see that synchronous exceptions never have any
     * of PC[8:7] set, but that IRQ/FIQ/SError exceptions *always* have at
     * least one of those bits set.
     */
    return is_exception(pc) && !(pc & 0x180);
}

void Context::set_sysreg_callbacks(sysreg_cb new_msr_callback, sysreg_cb new_mrs_callback)
{
    msr_callback = new_msr_callback;
    mrs_callback = new_mrs_callback;

    // Note: code elsewhere relies upon qemu_plugin_reset being called here. Do
    // not remove the next line without ensuring callers of
    // set_sysreg_callbacks() no longer rely upon this behavior.
    qemu_plugin_reset(this->plugin_id, reset_callback);
}

void Context::before_insn(unsigned int cpu_index, uint64_t pc, uint32_t encoding)
{
    if (last_insn_syscall || last_insn_eret || is_exception(pc) || first_instruction) {
        uint32_t pstate;

        // Determine what type of context update we are handling (and assume
        // non-synchronous exception
        ContextUpdateType update_type;
        if (last_insn_eret) {
            last_insn_eret = false;
            update_type = ContextUpdateType::ERET;
        } else if (last_insn_syscall || is_synchronous_exception(pc)) {
            last_insn_syscall = false;
            update_type = ContextUpdateType::SYNC_EXC;
        } else if (first_instruction) {
            first_instruction = false;
            update_type = ContextUpdateType::TRACING;
        } else {
            update_type = ContextUpdateType::ASYNC_EXC;
        }

        if (!qemu_plugin_read_reg32(&pstate, 33 /* CPSR/PSTATE */)) {
            log("Failed to read PSTATE to get current EL\n");
            exit(0);
        }

        current_el = (pstate >> 2) & 0x3;
        update_callback(current_el, current_pid, update_type);
        qemu_plugin_reset(this->plugin_id, reset_callback);
    }
    if (prev_mrs_encoding) {
        if (mrs_callback) {
            uint64_t val;
            if (!qemu_plugin_read_reg64(&val, MRS_REG(*prev_mrs_encoding))) {
                log("Failed to read sysreg value from register %" PRIu32 ".\n", MRS_REG(*prev_mrs_encoding));
                return;
            }

            SysReg sysreg = SysReg(MSR_OP0(*prev_mrs_encoding),
                                   MSR_OP1(*prev_mrs_encoding),
                                   MSR_CRN(*prev_mrs_encoding),
                                   MSR_CRM(*prev_mrs_encoding),
                                   MSR_OP2(*prev_mrs_encoding));
            mrs_callback(sysreg, val);
        }

        prev_mrs_encoding = std::nullopt;
        qemu_plugin_reset(this->plugin_id, reset_callback);
    }
    if (mrs_callback && IS_MRS(encoding) && (IS_MRS_REG(encoding, CNTVCT_EL0) || IS_MRS_REG(encoding, CNTPCT_EL0))) {
        prev_mrs_encoding = encoding;
        qemu_plugin_reset(this->plugin_id, reset_callback);
    }

    if (IS_ERET(encoding)) {
        last_insn_eret = true;
        qemu_plugin_reset(this->plugin_id, reset_callback);
    } else if (IS_SVC(encoding) || IS_HVC(encoding) || IS_SMC(encoding)) {
        last_insn_syscall = true;
        qemu_plugin_reset(this->plugin_id, reset_callback);
    } else if (IS_MSR(encoding)) {
        // register 31 maps to SP the way we are reading these from QEMU, but
        // is XZ for MSR's
        uint64_t val = 0;
        int reg = MSR_REG(encoding);
        if (reg != 31 && !qemu_plugin_read_reg64(&val, reg)) {
            log("Failed to read written sysreg value from register %" PRIu32 ".\n", MSR_REG(encoding));
            return;
        }

        if (IS_MSR_REG(encoding, CONTEXTIDR_EL1)) {
            current_pid = val;
            update_callback(current_el, current_pid, ContextUpdateType::CONTEXTIDR);
            qemu_plugin_reset(this->plugin_id, reset_callback);
        } else if (IS_MSR_REG(encoding, VBAR_EL1)) {
            vbar_el1 = val;
            qemu_plugin_reset(this->plugin_id, reset_callback);
        } else if (IS_MSR_REG(encoding, VBAR_EL2)) {
            vbar_el2 = val;
            qemu_plugin_reset(this->plugin_id, reset_callback);
        } else if (IS_MSR_REG(encoding, VBAR_EL3)) {
            vbar_el3 = val;
            qemu_plugin_reset(this->plugin_id, reset_callback);
        } else if (msr_callback) {
            SysReg sysreg =
                SysReg(MSR_OP0(encoding), MSR_OP1(encoding), MSR_CRN(encoding), MSR_CRM(encoding), MSR_OP2(encoding));
            msr_callback(sysreg, val);
        }
    }
}

uint64_t num_string(const json& jval)
{
    if (jval.is_string()) {
        return std::stoull(jval.get<std::string>(), nullptr, 0);
    } else
        return jval.get<uint64_t>();
}

inline std::string as_hex(uint64_t num)
{
    char buf[144];
    snprintf(buf, sizeof(buf), "0x%lx", num);
    return std::string(buf);
}

void Context::to_json(json& obj)
{
    obj["context_version"] = TRACING_CONTEXT_LATEST_VERSION;
    obj["vbar_el1_base"] = as_hex(vbar_el1);
    obj["vbar_el2_base"] = as_hex(vbar_el2);
    obj["vbar_el3_base"] = as_hex(vbar_el3);
    obj["current_el"] = current_el;
    obj["current_pid"] = current_pid;
}

bool Context::from_json(const json& jval)
{
    uint64_t context_version;

    if (!jval.count("context_version"))
        return false;
    context_version = num_string(jval["context_version"]);

    if (context_version < 1 || context_version > TRACING_CONTEXT_LATEST_VERSION)
        return false;

    if (!jval.count("vbar_el1_base") || !jval.count("vbar_el2_base") || !jval.count("vbar_el3_base")
        || !jval.count("current_el") || !jval.count("current_pid"))
        return false;

    vbar_el1 = num_string(jval["vbar_el1_base"]);
    vbar_el2 = num_string(jval["vbar_el2_base"]);
    vbar_el3 = num_string(jval["vbar_el3_base"]);
    current_el = num_string(jval["current_el"]);
    current_pid = num_string(jval["current_pid"]);

    update_callback(current_el, current_pid, ContextUpdateType::TRACING);

    return true;
}

} // namespace tracer
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
