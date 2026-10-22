# Copyright (c) 2022-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Generate performance validation collateral for RTL and panthera.

Generate the collateral for traces we are interested in debugging/generate more data for debugging.
"""

import argparse
import logging
import os
import subprocess
from concurrent.futures import ProcessPoolExecutor
from functools import partial
from multiprocessing import cpu_count
from os.path import join

import pandas as pd

from .. import install_dir, setup_logging
from .singular_debug_collateral import generate_collateral


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--rtl_es_folder", help="Folder where all RTL tasks reside", required=True)
    parser.add_argument(
        "--panthera_es_folder",
        help="Folder where Panthera event streams reside",
        required=True,
    )
    parser.add_argument(
        "--output_dir",
        help="This is where all traces To Debug will be created",
        required=True,
    )
    parser.add_argument(
        "--traces_to_debug_path",
        help="This is the list of traces that will data created for",
        required=True,
    )
    parser.add_argument(
        "--es_rtl_ts_start",
        type=int,
        default=0,
        help="This will change based on if warm up needed or not in the debug",
    )
    parser.add_argument(
        "--es_rtl_ts_end",
        type=int,
        default=30000,
        help="This will determine the stop point",
    )
    parser.add_argument(
        "--es_panthera_ts_start",
        type=int,
        default=0,
        help="This will change based on if warm up needed or not in the debug",
    )
    parser.add_argument(
        "--es_panthera_ts_end",
        type=int,
        default=30000,
        help="This will determine the stop point",
    )
    parser.add_argument(
        "--es_progress_event",
        default="sys.soc_0.core_0.gpc.instruction_commit",
        help="Event used in ES files for tracking progress",
    )
    parser.add_argument(
        "--es_pc_metric",
        default="sys.soc_0.core_0.gpc.commit_pc",
        help="Event used in ES files for commit PC",
    )
    parser.add_argument(
        "--es_inum_metric",
        default="sys.soc_0.core_0.gpc.inum",
        help="Event used in ES files for commit inum",
    )
    parser.add_argument(
        "--link",
        metavar="SUFFIX",
        action="append",
        help="Symlink files from event-stream folder in output folder as well",
    )
    parser.add_argument(
        "--skip_existing",
        action="store_true",
        help="Skip existing files",
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=30 * 60,
        help="Timeout of each step in seconds (0 to disable)",
    )
    parser.add_argument("--adis", action="store_true", help="Use ADIS for disasm")
    parser.add_argument(
        "-j",
        "--parallel",
        metavar="N",
        default=max(1, min(32, int(cpu_count() / 4))),
        help="Amount of threads used",
    )
    parser.add_argument("--debug", action="store_true", help="Print debug info")
    return parser.parse_args()


def match(
    trace_name,
    es1_file,
    es2_file,
    es1_label="es1",
    es2_label="es2",
    output_dir=".",
    es_start=None,
    es_end=None,
    event="sys.soc_0.core_0.gpc.instruction_commit",
    pc_metric="sys.soc_0.core_0.gpc.commit_pc",
    inum_metric="sys.soc_0.core_0.gpc.inum",
    timeout=None,
):
    output_folder = os.path.join(output_dir, trace_name)
    if not os.path.exists(output_folder):
        os.makedirs(output_folder)

    trace_file = join(output_folder, trace_name + "_" + trace_name + ".match.out")
    match_command = [
        join(install_dir, "bin/panthera"), "events.match",
        "--event", event,
        "--es0", es1_file,
        "--es1", es2_file,
        "--es0-label", es1_label,
        "--es1-label", es2_label,
        "--start", str(es_start),
        "--end", str(es_end),
        "--o", trace_file,
        "--pc_metric", pc_metric,
        "--inum_metric", inum_metric,
    ]  # fmt: skip

    logging.debug("Match: " + " ".join(match_command))
    subprocess.run(match_command, timeout=timeout)


def main(args):
    setup_logging(debug=args.debug)

    timeout = args.timeout if args.timeout and args.timeout > 0 else None

    rtl_files = generate_collateral(
        args.rtl_es_folder,
        args.traces_to_debug_path,
        name="RTL",
        output_dir=args.output_dir,
        es_start=args.es_rtl_ts_start,
        es_end=args.es_rtl_ts_end,
        es_progress_event=args.es_progress_event,
        skip_existing=args.skip_existing,
        link_suffixes=args.link,
        parallel=args.parallel,
        timeout=timeout,
        adis=args.adis,
    )

    panthera_files = generate_collateral(
        args.panthera_es_folder,
        args.traces_to_debug_path,
        name="Panthera",
        output_dir=args.output_dir,
        es_start=args.es_panthera_ts_start,
        es_end=args.es_panthera_ts_end,
        es_progress_event=args.es_progress_event,
        skip_existing=args.skip_existing,
        link_suffixes=args.link,
        parallel=args.parallel,
        timeout=timeout,
        adis=args.adis,
    )

    generate_match = partial(
        match,
        es1_label="RTL",
        es2_label="Panthera",
        output_dir=args.output_dir,
        es_start=0,
        es_end=args.es_rtl_ts_end - args.es_rtl_ts_start,
        event=args.es_progress_event,
        pc_metric=args.es_pc_metric,
        inum_metric=args.es_inum_metric,
        timeout=timeout,
    )

    traces = []
    rtl_traces = []
    panthera_traces = []
    for (trace_name, RTL_trace), (_trace_name, Panthera_trace) in zip(rtl_files, panthera_files):
        traces.append(trace_name)
        rtl_traces.append(RTL_trace)
        panthera_traces.append(Panthera_trace)

    with ProcessPoolExecutor(args.parallel) as pool:
        list(pool.map(generate_match, traces, rtl_traces, panthera_traces))


if __name__ == "__main__":
    main(parse_args())
