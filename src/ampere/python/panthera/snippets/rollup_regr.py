#!/usr/bin/env python3
# Copyright (c) 2020-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Rollup RTL regression.

Create a `panthera cheetah`-like directory structure from a RTL regression run
of snippets and calculate a rollup for it.
"""

import argparse
import concurrent.futures
import json
import logging
import os
import re
import shutil
import subprocess
import sys
import traceback
from pathlib import Path

import panthera.events.evp as evp
import panthera.rollup.__main__ as rollup
from panthera import setup_logging
from panthera.cheetah.cheetah_study import Study
from panthera.constants import SUMMARY_CSV_SUFFIX, TIMESERIES_CSV_SUFFIX

from .. import install_dir
from .regr_job import RegrJob


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output_dir", help="Output directory (will be created)")
    parser.add_argument("regr_dir", help="Directory containing the RTL regression output")
    parser.add_argument("manifest_json", help="Path to manifest.json for snippet release being run")
    parser.add_argument(
        "--ts_interval", default=100, help="Instruction count for timeseries interval (default 100 instructions)"
    )
    parser.add_argument(
        "--ts_start",
        type=int,
        default=10000,
        required=True,
        help="Instruction count to use as warmup (default 10,000 instructions)",
    )
    parser.add_argument(
        "--ts_stop",
        type=int,
        default=30000,
        help="Instruction count to use as end of measurement (default 30,000 instructions)",
    )
    parser.add_argument(
        "--ts_metric", default="sys.soc_0.core_0.gpc.instruction_commit", help="Instruction count event"
    )
    parser.add_argument("--evp-config", action="append", help="Event processor config")
    parser.add_argument("--rollup-formula", default="rtl_rollup_formula.py", help="Rollup formulas to compute")
    parser.add_argument(
        "--num-threads", help="Number of threads to use for converting event streams to CSV", type=int, default=5
    )
    parser.add_argument(
        "--truncate-run-log", type=int, metavar="LINES", nargs="?", default=0, const=100, help="Truncate *.run.log"
    )
    parser.add_argument("--move", action="store_true", help="Move files instead of copying")
    parser.add_argument("--summarize-all", action="store_true", help="Summarize full region as well")
    parser.add_argument("--debug", action="store_true", help="Set log level to DEBUG instead of INFO")

    args = parser.parse_args()
    if not args.evp_config:
        args.evp_config = ["rtl/base.cfg"]

    return args


def copy_truncated(src, dest, first=100, last=100):
    if os.path.exists(dest):
        os.unlink(dest)

    with open(dest, "a") as f:
        subprocess.run(["head", "-n", str(first), src], stdout=f)
        f.write(f"\n\n... truncated to first {first} and last {last} lines ...\n\n\n")
        f.flush()
        subprocess.run(["tail", "-n", str(last), src], stdout=f)


def process_snippet(local_output_dir, snippet, args):
    regr_task_dir = os.path.join(args.regr_dir, snippet["regr_dir"])
    regr_es = os.path.join(regr_task_dir, "events.es.xz")
    regr_log = os.path.join(regr_task_dir, "regr.log")
    run_log = os.path.join(regr_task_dir, "run.log")

    def _add_suffix(suffix):
        return os.path.join(local_output_dir, snippet["name"] + suffix)

    def copy(src, dest):
        if args.move:
            shutil.move(src, dest)
        else:
            shutil.copy2(src, dest)

    local_es = _add_suffix(".es.xz")
    if not os.path.exists(regr_es):
        regr_es = os.path.join(regr_task_dir, "events.es")
        local_es = _add_suffix(".es")

    local_regr_log = _add_suffix(".log")
    local_run_log = _add_suffix(".run.log")

    if not os.path.isfile(local_es):
        copy(regr_es, local_es)
    else:
        logging.info(local_es + " already exists")
    if not os.path.isfile(local_regr_log):
        copy(regr_log, local_regr_log)
    else:
        logging.info(local_regr_log + " already exists")
    if not os.path.isfile(local_run_log):
        if args.truncate_run_log > 0:
            copy_truncated(run_log, local_run_log, first=args.truncate_run_log, last=args.truncate_run_log)
            if args.move:
                os.remove(run_log)
        else:
            copy(run_log, local_run_log)
    else:
        logging.info(local_run_log + " already exists")

    ts = _add_suffix(f".{TIMESERIES_CSV_SUFFIX}")
    summary_csv = _add_suffix(f".{SUMMARY_CSV_SUFFIX}")

    meta = {
        "configuration": "regr",
        "suite": snippet["suite"],
        "weight": snippet["weight"],
        "workload": snippet["workload"],
    }

    try:
        if os.path.isfile(local_es):
            if not os.path.isfile(ts) and not os.path.isfile(ts + ".gz"):
                ts_stop = min(snippet.get("measurement_instructions", sys.maxsize), args.ts_stop)
                if (ts_stop - args.ts_start) <= 1:
                    ts_start = snippet.get("warmup_instructions", 0)
                else:
                    ts_start = args.ts_start

                logging.debug("Processing %s to %s (%d, %d)", local_es, ts, ts_start, ts_stop)
                evp_args = [
                    "-i", f"{args.ts_metric}={args.ts_interval}",
                    "--start", f"{args.ts_metric}={ts_start}",
                    "--stop", f"{args.ts_metric}={ts_stop}",
                    "--exit"
                ]  # fmt: skip
                evp_args += [f"@{install_dir}/config/events.processor/{cfg}" for cfg in args.evp_config]
                evp_args += [
                    "+summarize",
                    "--summary", summary_csv,
                    "--timeseries", ts,
                ]  # fmt: skip

                evp.run(local_es, evp_args, capture_output=True)

                if args.summarize_all:
                    evp_args = ["-i", f"{args.ts_metric}={args.ts_interval}"]
                    evp_args += [f"@{install_dir}/config/events.processor/{cfg}" for cfg in args.evp_config]
                    evp_args += [
                        "+summarize",
                        "--summary", _add_suffix(f".all.{SUMMARY_CSV_SUFFIX}"),
                        "--timeseries", _add_suffix(f".all.{TIMESERIES_CSV_SUFFIX}"),
                    ]  # fmt: skip

                    evp.run(local_es, evp_args, capture_output=True)
            else:
                logging.info(ts + " already exists")

            if os.path.isfile(ts) or not os.path.isfile(ts + ".gz"):
                logging.debug("Compressing " + ts)
                subprocess.run(["gzip", ts])
            else:
                logging.info(ts + ".gz" + " already exists")
        else:
            logging.warning(local_es + " is missing")
    except Exception as e:
        traceback.print_exc()
        logging.warning(f"Error when processing event stream {local_es}: {str(e)}")
        meta["event_stream_error"] = str(e)

    return RegrJob(
        name=snippet["name"],
        directory=os.path.basename(local_output_dir),
        study_directory=os.path.dirname(local_output_dir),
        meta=meta,
    )


def main():
    args = parse_args()
    setup_logging(debug=args.debug)

    Path(args.output_dir).mkdir(parents=True)
    local_output_dir = os.path.join(args.output_dir, "regr")
    Path(local_output_dir).mkdir(parents=True, exist_ok=True)

    manifest = []
    with open(args.manifest_json) as fp:
        manifest = json.load(fp)

    # Create a map from basenames to snippet objects for easy lookup
    snippets = {}
    for snippet in manifest:
        stem = Path(snippet["chkpt_file"]).resolve().stem
        snippets[stem] = snippet

    # Go through regression directory, looking for the +chkpt_file line and
    # saving the associated directory name on the 'snippets' dictionary
    chkpt_re = re.compile(r"^\+chkpt_file=.*/([^/]+)\.chkpt$")
    for d in [d.path for d in os.scandir(args.regr_dir) if d.is_dir()]:
        regr_es = os.path.join(d, "events.es")
        regr_es_xz = os.path.join(d, "events.es.xz")
        regr_log = os.path.join(d, "regr.log")
        run_log = os.path.join(d, "run.log")
        if (
            (os.path.exists(regr_es) or os.path.exists(regr_es_xz))
            and os.path.exists(regr_log)
            and os.path.exists(run_log)
        ):
            sknobs_path = os.path.join(d, "sknobs.save")
            with open(sknobs_path) as sknobs_file:
                for line in sknobs_file:
                    chkpt_base = chkpt_re.match(line)
                    if chkpt_base:
                        base = chkpt_base.group(1)
                        if base in snippets:
                            snippets[base]["regr_dir"] = os.path.basename(d)
                        else:
                            logging.warning(base + " was run but is missing from results")
                        break

    jobs = []

    with concurrent.futures.ProcessPoolExecutor(max_workers=args.num_threads) as executor:
        executors = []
        for snippet in snippets.values():
            if "regr_dir" in snippet:
                executors.append(executor.submit(process_snippet, local_output_dir, snippet, args))
        for executor_result in concurrent.futures.as_completed(executors):
            regr_job = executor_result.result()
            jobs.append(regr_job)

    study_json = os.path.join(args.output_dir, "study.json")
    study = Study(
        Path(args.output_dir).name,
        job_tree=jobs,
        study_directory=args.output_dir,
    )

    with open(study_json, "w") as f:
        json.dump(study.to_json(), f, indent=2)

    def summarize(rollup_args=None):
        rollup_args = rollup_args or []
        rollup.main(
            [
                study_json,
                "--outdir", args.output_dir,
                "--regr",
                "--formula", f"{install_dir}/config/rollup/{args.rollup_formula}",
                "--groupby", "trace",
                "--groupby", "workload",
                "--threads", str(args.num_threads),
            ]
            + rollup_args
        )  # fmt: skip

    summarize()
    if args.summarize_all:
        summarize(
            [
                "--output", "rollup.all.csv",
                "--summary-suffix", f".all.{SUMMARY_CSV_SUFFIX}",
                "--timeseries-suffix", f".all.{TIMESERIES_CSV_SUFFIX}",
            ]
        )  # fmt: skip


if __name__ == "__main__":
    main()
