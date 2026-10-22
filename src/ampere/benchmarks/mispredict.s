//
// Copyright (c) 2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

.globl mispredict_returns

.data
.align 16

.text
.align 6
mispredict_returns:
        // In this loop, the return will mispredict every time because we've modified x30 and so the
        // return address doesn't match the RSB. If the mispredict recovery time is N cycles, each
        // iteration of the loop will take N+2 cycles: N for the flush recovery, 1 cycle because the
        // sub is fetched a cycle after the bl, and 1 cycle because the ret depends on the sub.
        // So the IPC should be 3/(N+2).
        bl .Lnext_instr
.Lnext_instr:
        sub x30, x30, #4
        ret
