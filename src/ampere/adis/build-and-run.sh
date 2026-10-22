#!/bin/bash
#
# Copyright (c) 2023 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# this is run by CI and by the nixsh script
# run this (or ../nixsh) from the 'build' subdirectory
build_type=${1:-Release}
echo "Argument is ${build_type}"
# the ,, is a bash idiom for for lowercase
cmake -G Ninja -DCMAKE_BUILD_TYPE=${build_type} -DCMAKE_INSTALL_PREFIX=../install-${build_type,,} ..
ninja install
