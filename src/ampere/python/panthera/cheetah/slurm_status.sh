#!/bin/bash
# Copyright (c) 2022-2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


JOBID=$1
TOTAL=$2

while [ 1 ];  do
    S="$(sacct -X --noheader -j $JOBID --state BOOT_FAIL,CANCELLED,COMPLETED,DEADLINE,FAILED,NODE_FAIL,OUT_OF_MEMORY,PREEMPTED,RUNNING,REQUEUED,RESIZING,REVOKED,SUSPENDED,TIMEOUT -o 'State' | sed 's/\r//g' | sort | uniq -c)"
    # Adjust columns displayed by approximate size
    REPORTED=$(echo "$S" | awk '{ reported += $1 } END { printf("%d", reported) }')
    INCLUDED=$(( $(tput cols) / 28 ))
    if [ "$REPORTED" == "$TOTAL" ]; then
        INCLUDED=$(( INCLUDED + 1 ))
    fi

    echo -ne "\rUpdated: $(date '+%T'), "
    echo "$S"| \
        awk -v total="$TOTAL" -v to_include="$INCLUDED" '{
            included += 1
            reported += $1
            if (included<to_include) {
                printf("%s: %d, ", $2, $1);
            }
            if ($2=="COMPLETED") {
                completed=$1}
            }
        END {
            if (reported<total) {
                printf("PENDING: %d, ", total - reported, total);
            }
            printf("TOTAL: %d ... %.2f%% Complete.", total, completed / total * 100);
        }'

    # Clear to the end of the line
    echo -ne "\033[K"

    sleep 10s
done
