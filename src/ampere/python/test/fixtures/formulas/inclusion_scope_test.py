# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

include("scoped_formula_test.py", scope=metric.xxx)
include("scoped_formula_test.py", scope=metric.yyy)

metric.time_diff = metric.stop_time - metric.start_time
