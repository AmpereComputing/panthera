#! /bin/bash
# Copyright (c) 2025-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

# List all files matching patterns, excluding files with spaces and symlinks

git ls-files --exclude-standard --deduplicate "$@" | \
    grep -v ' ' | \
    while IFS= read -r file; do
        [[ -f "$file" ]] && realpath "$file"
    done | \
    uniq | \
	grep -v '/src/ampere/adis/' | \
	grep -v '/src/ampere/python/xlat/' | \
	grep -v '/src/effolkronium/' | \
	grep -v '/src/elfio/'
