#!/usr/bin/env python
# Copyright (c) 2023 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import random


def gen_test(fileobj, code_footprint_bytes, name):
    random.seed(5)  # chosen with a fair roll of the dice

    # 1 branch per cacheline, 64 bytes per cacheline, but subtract one to
    # account for the LOOP_END_LABEL being a branch/cacheline of its own
    num_branches = int(code_footprint_bytes / 64) - 1
    branch_list = [i for i in range(0, num_branches)]
    # Pseudo-randomly shuffle the list
    random.shuffle(branch_list)

    # Create a map of which branch label branches to which
    first_branch = branch_list[0]
    branch_map = dict(zip(branch_list, branch_list[-1:] + branch_list[:-1]))

    # Align this region to the expected code footprint (in bytes)
    fileobj.write(f"""
.globl code_streaming_{name}
code_streaming_{name}:
    b   BRANCH_LABEL_{name}_{first_branch}
.balign {code_footprint_bytes}
""")

    for branch_num in range(0, num_branches):
        next_branch_num = branch_map[branch_num]

        # if we would otherwise be wrapping back around to the beginning of the
        # cycle of branches, break out so we can check the loop counter
        next_branch_label = (
            f"LOOP_END_LABEL_{name}" if next_branch_num == first_branch else f"BRANCH_LABEL_{name}_{next_branch_num}"
        )
        fileobj.write(f"""
BRANCH_LABEL_{name}_{branch_num}:
    b   {next_branch_label}
    .rept 15
    nop
    .endr
""")

    fileobj.write(f"""
LOOP_END_LABEL_{name}:
    subs    x0, x0, 1
    bne     LOOP_RESTART_LABEL_{name}
    ret
LOOP_RESTART_LABEL_{name}:
    b   BRANCH_LABEL_{name}_{first_branch}
""")


def main():
    with open("code_streaming.S", "w") as fileobj:
        fileobj.write("""//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

// Stress the IL1 cache by creating randomized, but easily-predicted, branches
// across an aligned region of memory. 64k is large enough to thrash our 32Kb
// icache, but should still fit in the branch predictor(s).
""")
        gen_test(fileobj, 1024 * 8, "8k")
        gen_test(fileobj, 1024 * 16, "16k")
        gen_test(fileobj, 1024 * 32, "32k")
        gen_test(fileobj, 1024 * 64, "64k")
        gen_test(fileobj, 1024 * 128, "128k")


if __name__ == "__main__":
    main()
