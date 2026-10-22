#!/usr/bin/env python3
# Copyright (c) 2025-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


"""
Annotate the study list with max memory and cpu time from data of a previous run.

To use this script you must run the study list with options
`--slurm-array-count 1` and a `--slurm-default-mem` high enough that slurm
does not kill the jobs.  Note the jobid that panthera submitted as you will need
that when calling this script.


Example:

# Run study
`panthera study src/ampere/config/cheetah/python/core_study.py \
    --slurm-array-count 1 \
    --slurm-default-mem 8192 \
    --study reference \
    --workloads \
    /path/to/study-list.json`
INFO: 11:53:43 Submitted batch job(s) ['13513711']

# Annotate trace list
`panthera tracing.annotate_study_list \
    --job-id 13513711 \
    --job-txt reference/jobs_0.txt \
    --field-name amperesys \
    --trace-list /path/to/study-list.json \
    -o annotated_study_list.json`
"""

import argparse
import json
import math
import os

import pyslurm


def main():
    parser = argparse.ArgumentParser(description="print detailed trace information")

    parser.add_argument("-j", "--job-id", type=int, required=True, help="Job ID")
    parser.add_argument("-l", "--trace-list", type=str, required=True, help="Trace list to annotate")
    parser.add_argument(
        "-n",
        "--field-name",
        type=str,
        required=True,
        help='Name of field to add/update, for example use "amperesys" for panthera core study',
    )
    parser.add_argument("-o", "--out-trace-list", type=str, required=True, help="Trace list file to write")
    parser.add_argument("-s", "--sort", action="store_true", help="Sort study list by decreasing CPU usage")
    parser.add_argument("-t", "--job-txt", type=str, required=True, help="jobs.txt file associated with jobs.txt file")
    args = parser.parse_args()

    # Open jobs.txt to find trace name
    lines = []
    with open(args.job_txt, "r") as file:
        lines = [line.strip() for line in file]

    # Open trace list and put in dictionary with key being name
    trace_list = {}
    with open(args.trace_list, "r") as trace_file:
        trace_array = json.load(trace_file)
        trace_list = {trace["name"]: trace for trace in trace_array}

    # Get job information from slurm database
    j_filter = pyslurm.db.JobFilter(ids=[args.job_id])
    jobs = pyslurm.db.Jobs.load(j_filter)

    if jobs:
        for id, task in jobs.items():
            id = task.array_task_id
            job_name = os.path.basename(lines[id - 1])
            name = os.path.splitext(job_name)[0]

            stats = task.stats
            trace_data = trace_list[name]

            max_resident_mb = trace_data.get("max_resident_mb", {})
            max_resident_mb[args.field_name] = math.ceil(stats.max_resident_memory / 1024 / 1024)

            # Currently, nix-shell uses 800MB.  This is not captured since slurm does sampling and the
            # nix-shell spike is quick.
            if max_resident_mb[args.field_name] < 1024:
                max_resident_mb[args.field_name] = 1024

            trace_data["max_resident_mb"] = max_resident_mb

            total_cpu_secs = trace_data.get("total_cpu_secs", {})
            total_cpu_secs[args.field_name] = stats.elapsed_cpu_time
            trace_data["total_cpu_secs"] = total_cpu_secs

    with open(args.out_trace_list, "w") as trace_file:
        t = list(trace_list.values())
        if args.sort:
            t = sorted(t, key=lambda x: x["total_cpu_secs"]["amperesys"], reverse=True)
        json.dump(t, trace_file, indent=4)


if __name__ == "__main__":
    main()
