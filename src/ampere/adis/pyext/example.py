#!/usr/bin/env python3
#
# Copyright (c) 2022-2023 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#

import adis

help(adis)
print(adis.dis(0x11223344))
print(adis.is_sve(0x11223344))
sve_add = 0b00000100_00_1_00000_000_000_00000_00000
print(adis.dis(sve_add))
print(adis.is_sve(sve_add))
