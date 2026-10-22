#! /bin/bash
# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


if [[ "$1" == "-h" || "$1" == "--help" || "$1" == "" ]]; then
    echo "usage $0: [--fix] patterns..."
    echo
    echo "Check and/or fix tabs to spaces in files matching patterns."
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
    tabs=$("$CHECKS/files.sh" "$@" | xargs grep -Pn '\t' | tee "$LOGFILE" | wc -l)
    if [[ "$tabs" != "0" ]]; then
        files=$(cat "$LOGFILE" | sed -e 's/:.*//' | sort -u | wc -l)
        echo "Files containing tabs ($files):"
        echo
        cat "$LOGFILE"
        exit 1
    fi
else
	"$CHECKS/files.sh" "$@" | xargs sed -i 's/\t/    /g'
fi
