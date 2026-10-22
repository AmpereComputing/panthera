#!/usr/bin/env python3
# Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


"""
Consumes a tracing manifest file and creates a trace list for studies

arg1: tracebase
arg2: manifest.json, output from capture_traces.py or similar
arg3: tracelist.json, new output in tracelist format
arg4: suite name to use for tracelist
"""

import argparse
import json
import logging
import os

from panthera import setup_logging


def parse_args():
    parser = argparse.ArgumentParser(
        description="Consumes a tracing manifest file and creates a trace list for studies"
    )
    parser.add_argument("workload", help="Trace-base or workload")
    parser.add_argument("manifest", help="manifest.json, output from capture_traces.py or similar")
    parser.add_argument("tracelist", help="tracelist.json, new output in tracelist format")
    parser.add_argument("suite", help="suite name to use for tracelist")

    return parser.parse_args()


def main(args):
    setup_logging()
    intervals = []

    with open(args.manifest, "r") as manifest_file:
        manifest_json = json.load(manifest_file)

    optional_trace_files = [
        "pan_cache_preload_file",
        "pan_branch_warmup_file",
    ]
    missing_optional = {file: set() for file in optional_trace_files}

    for stream in manifest_json["traces"]:
        interval = {
            "name": stream["id"],
            "workload": args.workload,
            "weight": float(stream["weight"]),
            "file": os.path.join(args.workload, stream["file"]),
            "suite": args.suite,
        }

        for file in optional_trace_files:
            if file in stream:
                interval[file] = os.path.join(args.workload, stream[file])
            else:
                missing_optional[file].add(stream["id"])

        intervals.append(interval)

    for file, names in missing_optional.items():
        if names:
            logging.warning(f"Missing {file} for:" + "\n" + "\n".join(names))

    with open(args.tracelist, "w") as jsonout:
        json.dump(intervals, jsonout, indent=4)


if __name__ == "__main__":
    main(parse_args())
