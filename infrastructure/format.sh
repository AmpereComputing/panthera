#!/bin/bash
# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


set -e

FILENAME="$1"
if [[ "$FILENAME" == *".cc" || "$FILENAME" == *".h" ]]; then
    clang-format -style="file:$PWD/.clang-format" -i "$FILENAME"
elif [[ "$FILENAME" == *".py" ]]; then
	ruff check -q --config="$PWD/pyproject.toml" --select I --fix "$FILENAME"
	ruff format -q --config="$PWD/pyproject.toml" "$FILENAME"
fi
