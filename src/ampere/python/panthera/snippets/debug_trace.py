#!/usr/bin/env python3
# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Creates various debug information for performance validation.
"""

import argparse
import os
import subprocess
from itertools import chain
from os.path import join

from .. import install_dir

global_config_files = [
    "config/events.tracing/perfval.cfg",
]


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("es1", help="The event stream to create the collateral for")
    parser.add_argument(
        "--which",
        default="panthera",
        choices=("rtl", "panthera"),
        help="The event stream to create the collateral for is rtl or panthera event stream",
    )
    parser.add_argument("--output_dir", help="Directory containing the collateral output")
    parser.add_argument(
        "--timeout",
        type=int,
        default=30 * 60,
        help="Timeout of each step in seconds (0 to disable)",
    )
    parser.add_argument("--adis", action="store_true", help="Use ADIS for disasm")
    args = parser.parse_args()
    return args


def tracing_file(es_file, trace_name, output_dir, which, configs=None, suffix=None, skip_existing=False, timeout=None):
    config_files = configs or {
        "rtl": [
            "config/events.tracing/rtl_etrack/base.cfg",
            "config/events.tracing/rtl_etrack/base_pmu.cfg",
        ],
        "panthera": [
            "config/events.tracing/summary.cfg",
            "config/events.tracing/idr_detailed.cfg",
            "config/events.tracing/summary_pmu.cfg",
        ],
    }

    panthera_bin = join(install_dir, "bin/panthera")
    configs = [f"@{join(install_dir, cfg)}" for cfg in (global_config_files + config_files[which])]
    suffix = f".{suffix}" if suffix else ""
    output_file = join(output_dir, trace_name + f"{suffix}.json.gz")

    if not os.path.isfile(output_file) or not skip_existing:
        tracing_command = [
            panthera_bin, "events.tracing",
            "-c", es_file,
            "-o", output_file,
        ] + configs  # fmt: skip
        subprocess.run(tracing_command, check=True, timeout=timeout)

    return output_file


def rob_view(es_file, trace_name, output_dir, which, skip_existing=False, timeout=None):
    config_files = {
        "panthera": ["config/events.tracing/tx_rob.cfg"],
        "rtl": ["config/events.tracing/rtl_etrack/tx_rob.cfg"],
    }
    return tracing_file(
        es_file,
        trace_name,
        output_dir,
        which,
        configs=config_files,
        suffix="rob",
        skip_existing=skip_existing,
        timeout=timeout,
    )


def richasm(es_file, trace_name, output_dir, skip_existing=False, timeout=None, adis=False):
    richasm_columns = [
        "lsu.ofb.allocate",
        "lsu.pcp.launch",
        "lsu.uop_ld_recycle",
        "lsu.uop_ld_st_fwd",
        "lsu.uop_ld_raw_scrub_detected",
        "l2c.prefetcher.launch",
        "l2c.prefetcher.drop",
    ]
    richasm_command = [join(install_dir, "bin/es_richasm"), "-e", "99999999", "-x", "sys.soc_0.core_0"] + list(
        chain.from_iterable([["-m", col] for col in richasm_columns])
    )
    if adis:
        richasm_command.append("-a")
    richasm_command.append(es_file)

    output_file = join(output_dir, trace_name + ".richasm.txt")

    if not os.path.isfile(output_file) or not skip_existing:
        with open(output_file, "w") as richasm_path:
            subprocess.run(richasm_command, stdout=richasm_path, check=True, timeout=timeout)

    return output_file


def main():
    args = parse_args()
    if not os.path.exists(args.output_dir):
        os.makedirs(args.output_dir)

    timeout = args.timeout if args.timeout and args.timeout > 0 else None
    es1_name = os.path.splitext(os.path.basename(args.es1))[0]
    richasm(args.es1, es1_name, args.output_dir, adis=args.adis)
    tracing_file(args.es1, es1_name, args.output_dir, args.which, timeout=timeout)


def generate_files(es_trace, trace_name, output_dir, which, skip_existing=False, timeout=None, adis=False):
    files = []
    try:
        files.append(richasm(es_trace, trace_name, output_dir, skip_existing=skip_existing, timeout=timeout, adis=adis))
    except Exception as error:
        print("An exception occurred while creating richasm for: ", es_trace, ", Error:", error)

    try:
        files.append(
            tracing_file(es_trace, trace_name, output_dir, which, skip_existing=skip_existing, timeout=timeout)
        )
    except Exception as error:
        print("An exception occurred while creating tracing-.json for: ", es_trace, ", Error:", error)

    try:
        files.append(rob_view(es_trace, trace_name, output_dir, which, skip_existing=skip_existing, timeout=timeout))
    except Exception as error:
        print("An exception occurred while creating ROB tracing-.json for: ", es_trace, ", Error:", error)

    return files


if __name__ == "__main__":
    main()
