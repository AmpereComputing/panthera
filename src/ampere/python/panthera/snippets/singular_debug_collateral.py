# Copyright (c) 2024-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Generate performance validation collateral.

Generate the collateral for traces we are interested in debugging/generate more data for debugging.
"""

import argparse
import logging
import os
import re
import subprocess
import sys
from concurrent.futures import ProcessPoolExecutor
from functools import partial
from multiprocessing import cpu_count
from os.path import join

import pandas as pd

from .. import install_dir, setup_logging
from . import debug_trace


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--name", help="Name for collateral", required=True)
    parser.add_argument("--es_folder", help="Folder where all event streams reside", required=True)
    parser.add_argument(
        "--output_dir",
        help="This is where all traces To Debug will be created",
    )
    parser.add_argument(
        "--traces_to_debug_path",
        help="This is the list of traces that will data created for",
        required=True,
    )
    parser.add_argument(
        "--es_start",
        type=int,
        default=0,
        help="This will change based on if warm up needed or not in the debug",
    )
    parser.add_argument(
        "--es_end",
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
    parser.add_argument(
        "--adis",
        action="store_true",
        help="Use ADIS for disasm",
    )
    parser.add_argument(
        "-j",
        "--parallel",
        metavar="N",
        default=max(1, min(32, int(cpu_count() / 4))),
        help="Amount of threads used",
    )
    parser.add_argument("--debug", action="store_true", help="Print debug info")
    return parser.parse_args()


def create_symbolic_link(original_path, target_path, relative_to=None):
    try:
        original_path = os.path.abspath(original_path)
        target_path = os.path.abspath(target_path)
        if relative_to is not None:
            original_path = os.path.relpath(original_path, relative_to)
        os.symlink(original_path, target_path)
    except OSError as e:
        logging.warning(f"Error creating symbolic link: {e}")


def es_file_path(path):
    for suffix in ("", ".xz"):
        path += suffix
        if os.path.isfile(path):
            return path

    return path


def crop_es(
    trace,
    inum_to_skip_to,
    max_instr_to_crop,
    event="sys.soc_0.core_0.gpc.instruction_commit",
    skip_existing=True,
    timeout=None,
):
    output_trace_name = join(os.path.dirname(trace), "CROPPED_" + os.path.basename(trace))
    if not output_trace_name.endswith(".xz"):
        output_trace_name += ".xz"

    trace_summary_csv = re.sub(r"\.es(\.xz)?$", ".summary.csv", trace)
    if os.path.isfile(trace_summary_csv):
        metrics = pd.read_csv(trace_summary_csv).iloc[0]
        max_instr_to_crop = min(metrics.get("measurement_instructions", sys.maxsize), max_instr_to_crop)
        if (max_instr_to_crop - inum_to_skip_to) <= 1:
            inum_to_skip_to = metrics.get("warmup_instructions", 0)

    if not os.path.isfile(output_trace_name) or not skip_existing:
        crop_command = [
            join(install_dir, "bin/evp"),
            "--es", trace,
            "--start", f"{event}={inum_to_skip_to}",
            "--stop", f"{event}={max_instr_to_crop}",
            "--exit",
            "+count", "-a",
            "+capture", output_trace_name,
        ]  # fmt: skip

        logging.debug("Crop: " + " ".join(crop_command))
        subprocess.run(
            crop_command,
            capture_output=True,
            timeout=timeout,
        )

    return output_trace_name


def generate_trace_collateral(
    es_filename,
    trace_name,
    name=None,
    output_dir=None,
    es_start=None,
    es_end=None,
    es_progress_event=None,
    skip_existing=True,
    link_suffixes=None,
    timeout=None,
    adis=False,
):
    prefix = f"{name}_" if name else ""
    name = name.lower() if name else ""
    link_suffixes = link_suffixes or []

    logging.debug(f"create {name} cropped ES: {trace_name}")
    cropped = crop_es(
        es_filename,
        es_start,
        es_end,
        event=es_progress_event,
        skip_existing=skip_existing,
        timeout=timeout,
    )

    logging.debug(f"create tracing/richasm {name}: {trace_name}")
    debug_files = debug_trace.generate_files(
        cropped,
        trace_name,
        os.path.dirname(es_filename),
        name or "panthera",
        skip_existing=skip_existing,
        timeout=timeout,
        adis=adis,
    )

    if output_dir:
        output_dir = os.path.join(output_dir, trace_name)
        if not os.path.exists(output_dir):
            os.makedirs(output_dir)

        source_dir = os.path.dirname(es_filename)
        logfile = os.path.join(source_dir, f"{trace_name}.log")
        try:
            trace_path = None
            with open(logfile, "r") as lf:
                trace_file_re = re.compile(r"[^ ]+\.is(\.xz)?")
                for line in lf:
                    if m := trace_file_re.search(line):
                        trace_path = m.group(0)
                        break

            if trace_path and os.path.exists(trace_path):
                logfile_link = os.path.join(output_dir, f"{os.path.basename(trace_path)}")
                if not os.path.exists(logfile_link):
                    create_symbolic_link(os.path.abspath(trace_path), logfile_link)

        except FileNotFoundError:
            pass

        create_symbolic_link(
            es_filename,
            os.path.join(output_dir, f"{prefix}{trace_name}.all.es.xz"),
            relative_to=output_dir,
        )
        create_symbolic_link(
            cropped,
            os.path.join(output_dir, f"{prefix}{trace_name}.es.xz"),
            relative_to=output_dir,
        )

        for file in debug_files:
            create_symbolic_link(
                file,
                os.path.join(output_dir, f"{prefix}{os.path.basename(file)}"),
                relative_to=output_dir,
            )

        for suffix in link_suffixes:
            source_file = os.path.join(source_dir, f"{trace_name}{suffix}")
            if os.path.isfile(source_file):
                create_symbolic_link(
                    source_file,
                    os.path.join(output_dir, f"{prefix}{trace_name}{suffix}"),
                    relative_to=output_dir,
                )

    return trace_name, cropped


def generate_collateral(
    es_folder,
    traces,
    name=None,
    output_dir=None,
    es_start=None,
    es_end=None,
    es_progress_event=None,
    skip_existing=True,
    link_suffixes=None,
    parallel=None,
    timeout=None,
    adis=False,
):
    if isinstance(traces, str):
        with open(traces, "r") as file:
            traces = [line.strip() for line in file]

    traces_to_debug = {trace: es_file_path(os.path.join(es_folder, trace + ".es")) for trace in traces}

    generate = partial(
        generate_trace_collateral,
        name=name,
        output_dir=output_dir,
        es_start=es_start,
        es_end=es_end,
        es_progress_event=es_progress_event,
        skip_existing=skip_existing,
        link_suffixes=link_suffixes,
        timeout=timeout,
        adis=adis,
    )

    with ProcessPoolExecutor(parallel) as pool:
        results = pool.map(generate, traces_to_debug.values(), traces_to_debug.keys())

    return results


def main(args):
    setup_logging(debug=args.debug)
    timeout = args.timeout if args.timeout and args.timeout > 0 else None
    list(
        generate_collateral(
            args.es_folder,
            args.traces_to_debug_path,
            name=args.name,
            output_dir=args.output_dir,
            es_start=args.es_start,
            es_end=args.es_end,
            es_progress_event=args.es_progress_event,
            skip_existing=args.skip_existing,
            link_suffixes=args.link,
            parallel=args.parallel,
            timeout=timeout,
            adis=args.adis,
        )
    )


if __name__ == "__main__":
    main(parse_args())
