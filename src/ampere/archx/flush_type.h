//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/util/enum.h"
#include <cassert>

make_ns_enum_without_class_with_type(ampere::archx, FlushType, unsigned,
    /* Flush types shared with RTL. These are pulled directly from
     * MAIN/rtl/Subsystems/Cpu/CpuPkg.sv in the RTL repository. If updated, it
     * is useful to also synchronize these against
     * src/ampere/python/panthera/events/mapping.py */
    RESET = 0,
    DBG_HALT = 1,
    SVC = 2,
    HVC = 3,
    SMC = 4,
    ERET = 6, // called 'RETURN' in RTL, renamed here due to conflict with BranchType::RETURN
    INSTR_DEBUG = 7,
    DATA_DEBUG = 8,
    BR_MISPREDICT = 9,
    ICF_INSTR_FAULT = 10,
    IDR_INSTR_FAULT = 11,
    LSU_DATA_FAULT = 12,
    IXU_PAC_FAULT = 13,
    WAIT_STATE_EXIT = 14,
    IRQ = 15,
    FIQ = 16,
    RAW_SCRUB = 17,
    SP_ALIGN = 18,
    TRAP = 19,
    CSF_WTH_CMT = 20,  // Context-synchronizing-flush after commit (like ISB)
    NO_RESTART = 21,
    CSF_NO_CMT = 22,  // Context-synchronizing-flush before commit (like a fault)
    NO_RESTART2 = 23,
    DBG_RESTART = 24,
    LIVELOCK_BRK_THRES1 = 25,
    LIVELOCK_BRK_THRES2 = 26,
    /* Panthera-specific flush types */
    NOT_KNOWN = 103, // non-sequential instructions which aren't
                   // otherwise classified generate these
    ABORTED = 104, // snoop hits
//  ARCH_STATE_UPDATE = 105, // no longer used
    BRANCH_CHECK_UNCOND_BRANCH   = 106,
    BRANCH_CHECK_COND_BRANCH     = 107,
    BRANCH_CHECK_CALL_DIRECT     = 108,
    BRANCH_CHECK_CALL_INDIRECT   = 109,
    BRANCH_CHECK_UNCOND_INDIRECT = 110,
    BRANCH_CHECK_RETURN          = 111,
    BRANCH_CHECK_TARGET          = 112,
    BRANCH_CHECK_WRONG_TYPE      = 113,
    LOOPING_TLBI                 = 120
);

namespace ampere::archx {

inline bool is_frontend_flush(FlushType f)
{
    switch (f) {
    case BRANCH_CHECK_UNCOND_BRANCH:
    case BRANCH_CHECK_COND_BRANCH:
    case BRANCH_CHECK_CALL_DIRECT:
    case BRANCH_CHECK_CALL_INDIRECT:
    case BRANCH_CHECK_UNCOND_INDIRECT:
    case BRANCH_CHECK_RETURN:
    case BRANCH_CHECK_TARGET:
    case BRANCH_CHECK_WRONG_TYPE:
        return true;
    default:
        return false;
    }
}

inline bool is_nonspeculative_flush(FlushType f)
{
    // I don't think checking if a flush is speculative or not makes sense in a
    // context where you would expect to see branch checks (between ICF and
    // BPU)
    assert(!is_frontend_flush(f));

    return (f != BR_MISPREDICT) && (f != RAW_SCRUB);
}

} // namespace ampere::archx

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
