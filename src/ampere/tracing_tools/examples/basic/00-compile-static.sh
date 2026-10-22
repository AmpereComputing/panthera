# Copyright (c) 2022 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

# FIRST copy tracing_hints.h from src/ampere/tracing_tools

gcc -march=armv8.5-a -O3 basic.c -o basic -lm -static
