//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
.align 8

double_branch_loop:
    mov x18, 0x1000
loop:
    subs x18, x18, 1
    beq done
    b loop
done:
    ret

.global double_branch_loop

