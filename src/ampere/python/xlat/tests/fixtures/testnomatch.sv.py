# Copyright (c) 2018-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

# noqa: F403, F405
from ampere.xlat import scope, uops


@scope(uops)
def NoUop(uop):
    return False


@scope(NoUop, inputs='opcode')
def nomatch(uop):
    return False
