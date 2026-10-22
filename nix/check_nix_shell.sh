#!/bin/bash
# Copyright (c) 2023-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


if [[ -z "$IN_NIX_SHELL" ]]; then
    if [[ "$(echo $PATH | grep '/nix/store/')" != "" ]]; then
        echo "Not in Nix Shell, but PATH has some nix-like paths"
        exit 1
    fi
fi

if [[ "$IN_NIX_SHELL" == "$CMAKE_NIX_SHELL" ]]; then
    exit 0
fi

echo "In Nix Shell, but configured for non nix shell.  Or vice versa"
exit 1
