#!/usr/bin/env nix-shell
#! nix-shell -i python3 ../project.nix
#
# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#

import platform
import sys
assert len(sys.argv) == 2
build_kind = sys.argv[1]
sys.path.append(f'build-{build_kind}/pyext')
sys.path.append('pyext')
import adis
print(f"{platform.python_version_tuple()=}");
print(adis.dis(0x44444444))
r=adis.decode(0x44444444)
print(r)
s=adis.int2encoding_enum(r)
print(s)
print(adis.str2encoding_enum(s))
print(adis.is_sve_family(r))
print(adis.is_sve2(r))
print(adis.is_sve(r))
