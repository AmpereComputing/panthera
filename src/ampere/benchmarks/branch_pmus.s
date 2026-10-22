//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
        .macro PROLOGUE
        stp     x29, x30, [sp, #-16]!
        mov     x29, sp
        .endm

        .macro EPILOGUE
        ldp     x29, x30, [sp], #16
        .endm

        .macro START_TRACING
        mov     x0, #0x1                        // #1
        stp     x0, x1, [sp, #-16]!
        hint    #0x55
        ldp     x0, x1, [sp], #16
        isb
        .endm

        .macro STOP_TRACING
        mov     x0, #0x2                        // #2
        stp     x0, x1, [sp, #-16]!
        hint    #0x55
        ldp     x0, x1, [sp], #16
        .endm

        .macro ADDPAD repeat=1
        .if \repeat
        add x1, x1, x1
        ADDPAD "\repeat-1"
        .endif
        .endm

        # Novel backward conditional branches should result in mispredictions (the
        # default prediction for any never before seen branch is not-taken).
        #
        # Expectation: pmu.br_immed_mis_pred_retired = 16
        #
        .align 8
test_br_immed_mis_pred_retired:
        .global test_br_immed_mis_pred_retired
        cmp x10, x10
        b 1f
2:      ret
0:      beq 2b
2:      beq 0b
0:      beq 2b
2:      beq 0b
0:      beq 2b
2:      beq 0b
0:      beq 2b
2:      beq 0b
0:      beq 2b
2:      beq 0b
0:      beq 2b
2:      beq 0b
0:      beq 2b
2:      beq 0b
0:      beq 2b
1:      beq 0b


        # Novel unconditional branches will miss the BTB. This is a speculative
        # counter, so we need to pad with enough instructions to not see the
        # branches speculatively.
        #
        # Expectation: pmu.unconditional_branch_miss_btb = 7
        #
        .align 8
test_unconditional_branch_miss_btb:
        .global test_unconditional_branch_miss_btb
        PROLOGUE
        START_TRACING
        mov x8, 2
top:    b 0f
        ADDPAD 16
0:      b 0f
        ADDPAD 16
0:      b 0f
        ADDPAD 16
0:      bl 1f
        ADDPAD 16
        b 0f
        ADDPAD 16
0:      b 0f
        ADDPAD 16
0:      subs x8, x8, 1
        bne top
        ADDPAD 16
        STOP_TRACING
        ADDPAD 16
        ADDPAD 16
        ADDPAD 16
        ADDPAD 16
        ADDPAD 16
        ADDPAD 16
        ADDPAD 16
        ADDPAD 16
        EPILOGUE
        ret

1:      ret
        ADDPAD 16


        # These branches muck with x30 directly so they don't return to
        # where they're supposed to.
        #
        # Expectation: pmu.bpu_hit_rsb_and_mispredict: 4
        #              pmu.br_return_mis_pred_retired: 4
        .align 8
test_return_mispredict:
        .global test_return_mispredict
        PROLOGUE
        START_TRACING
        bl f1
        ADDPAD 16
0:      bl f2
        ADDPAD 16
1:      bl f3
        ADDPAD 16
2:      bl f4
        ADDPAD 16
3:      STOP_TRACING
        mov x10, 1024
4:      ADDPAD 1
        subs x10, x10, 1
        bne 4b
        EPILOGUE
        ret

f1:     adr x30, 0b
        ret

f2:     adr x30, 1b
        ret

f3:     adr x30, 2b
        ret

f4:     adr x30, 3b
        ret


        # Count indirect branches which hit the BTB but mispredict.
        # There are three styles:
        #
        #   1. A normal unconditional indirect which keeps going
        #      to a different target.
        #
        #   2. An indirect call which keeps going to a different
        #      target.
        #
        #   3. A return which keeps returning to a different target.
        #
        # In each case the first branch occurrence will of course
        # miss the BTB, but subsequence occurrences should hit.
        #       
        # Expectation:
        #    pmu.bpu_indirect_branch_hit_btb_and_mispredict = 13
        #    
        #
        .align 8
test_indirect_branch_hit_btb_and_mispredict:
        .global test_indirect_branch_hit_btb_and_mispredict
        PROLOGUE
        START_TRACING

        # 1: plain unconditional indirect
        mov x10, 6
        adr x8, branch_back

uncond_indir_loop:
        br x8
uncond_return:
        add x8, x8, 64
        subs x10, x10, 1
        bne uncond_indir_loop
        ADDPAD 16

        # 2: call indirect
        mov x10, 6
        adr x8, return_back

call_indir_loop:        
        blr x8
call_return:    
        add x8, x8, 64
        subs x10, x10, 1
        bne call_indir_loop
        ADDPAD 16

        # 3: returns
        adr x8, ret_target
ret_target:
        add x8, x8, 12
        bl flaky_return
        nop
        add x8, x8, 12
        bl flaky_return
        nop
        add x8, x8, 12
        bl flaky_return
        nop
        add x8, x8, 12
        bl flaky_return
        nop
        
        STOP_TRACING
        mov x10, 1024
4:      ADDPAD 1
        subs x10, x10, 1
        bne 4b
        EPILOGUE
        ret

        .p2align 6
branch_back:
        b uncond_return
        ADDPAD 15
        b uncond_return
        ADDPAD 15
        b uncond_return
        ADDPAD 15
        b uncond_return
        ADDPAD 15
        b uncond_return
        ADDPAD 15
        b uncond_return
        ADDPAD 15

return_back:
        ret
        ADDPAD 15
        ret
        ADDPAD 15
        ret
        ADDPAD 15
        ret
        ADDPAD 15
        ret
        ADDPAD 15
        ret
        ADDPAD 15

flaky_return:
        mov x30, x8
        ret
