# Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

core = metric.top.core

# Detailed branch misprediction breakdown

core.branch.mispredict_conditional.pki = 1000 * (core.branch_mispredict_conditional / core.bpu.br_train)
core.branch.mispredict_indirect.pki = 1000 * (
    (core.branch_mispredict_call_indirect + core.branch_mispredict_unconditional_indirect) / core.bpu.br_train
)
core.branch.mispredict_direct.pki = 1000 * (
    (core.branch_mispredict_unconditional_direct + core.branch_mispredict_call_direct) / core.bpu.br_train
)
core.branch.mispredict_return.pki = 1000 * (core.branch_mispredict_return / core.bpu.br_train)

core.branch.mispredict_btb_miss.pki = 1000 * (core.branch_mispredict_btb_miss / core.bpu.br_train)
