#! /bin/bash
# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


if [[ "$1" == "-h" || "$1" == "--help" || "$1" == "" ]]; then
    echo "usage $0: [--fix] patterns..."
    echo
    echo "Check and/or fix ending newlines in files matching patterns."
    exit 1
fi

CHECKS=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
FIX=
if [[ "$1" == "--fix" ]]; then
    FIX=1
    shift
fi

LOGFILE=$(mktemp)
trap 'rm -f $LOGFILE' EXIT

if [[ -z "$FIX" ]]; then
    missing=$("$CHECKS/files.sh" "$@" | xargs tail -c1 | grep -P -v '^$' | grep -v '=' -B 1 | tee "$LOGFILE" | grep -c '=')
    if [[ "$missing" != "0" ]]; then
        echo "Files missing ending newline ($missing):"
        echo
        cat "$LOGFILE" | grep '=' | awk '{ print $2 }'
        exit 1
    fi
else
	"$CHECKS/files.sh" "$@" | xargs sed -i '$a\'
fi
