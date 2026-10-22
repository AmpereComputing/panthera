# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import evp


def print_param(name):
    value = evp.get_parameter(name)
    print(type(value).__name__, value)


print_param("machine_width")
print_param("rob.size")
print_param("sys.soc_0.core_0.scheduler_configuration")
print_param("missing")
