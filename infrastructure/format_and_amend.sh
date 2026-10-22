#!/bin/bash
# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


TARGET=format
MAKE_ARGS=
if [[ "$1" == "-c" ]]; then
	TARGET=format-changed
	MAKE_ARGS=CHANGED_LAST_ONLY=1
fi

set -e

COMMIT=$(git rev-parse HEAD)

# Update submodules
git submodule update --init --recursive &> /dev/null

# Format C++ and commit
echo "Format C++ for $COMMIT (at clang-format.log)"
nix-shell --run "make clang-$TARGET $MAKE_ARGS" &> clang-format.log
rm clang-format.log
git commit -q -a --amend --no-edit

# Format Python and commit
echo "Format Python for $COMMIT (at python-format.log)"
nix-shell --run "make python-$TARGET $MAKE_ARGS" &> python-format.log
rm python-format.log
git commit -q -a --amend --no-edit
