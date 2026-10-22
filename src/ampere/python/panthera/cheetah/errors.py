#!/usr/bin/env python3
# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


"""Bucket and summarize errors from a study."""

import argparse
import csv
import logging
import shlex
import sys
from glob import glob
from multiprocessing import cpu_count
from pathlib import Path

from panthera import setup_logging
from panthera.cheetah import RollupStudy
from panthera.cheetah.bucketing import Bucketer
from panthera.cheetah.logfile import LogParsing

SORT_OPTIONS = ["walltime", "cputime", "tick"]


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("path", nargs="*", metavar="PATH", help="study directory, log files, or JSON file")
    parser.add_argument("--study", dest="study", type=argparse.FileType("r"), help="study JSON file")
    parser.add_argument("--format", choices=["table", "list", "CSV"], default="list", help="output format type")
    parser.add_argument("-o", "--output", type=argparse.FileType("a"), default=sys.stdout, help="output file name")

    per_group = parser.add_mutually_exclusive_group()
    per_group.add_argument("-c", "--per-config", action="store_true", help="Group per configuration")
    per_group.add_argument("--per", metavar="META", help="Group per meta-value")

    parser.add_argument("-j", "--jobs-per-bucket", default=0, type=int, help="Jobs to emit per bucket")
    parser.add_argument(
        "--sort-jobs-by", default="infer", choices=["infer"] + SORT_OPTIONS, help="Sort jobs by specified metric"
    )
    parser.add_argument("--ignore-incomplete", action="store_true", help="Ignore incomplete jobs")
    parser.add_argument("-s", "--summarize", action="store_true", help="Summarize pass/failure rate")

    parser.add_argument("--line-length", default=78, type=int, help="maximum line length in table/CSV format")
    parser.add_argument(
        "--parallel",
        metavar="N",
        type=int,
        default=max(1, min(8, int(cpu_count() / 4))),
        help="Amount of threads used for logfile parsing",
    )

    parser.add_argument("--debug", action="store_true", help="Enable debug logging")

    args = parser.parse_args()
    if args.per_config:
        args.per = "configuration"
    if args.summarize and args.format in {"CSV"}:
        raise argparse.ArgumentError(args.summarize, f"Summarize cannot be used with {args.format} format")
    if not args.path and not args.study:
        parser.error("Either --study or PATH should be provided")

    return args


def header(title, char="="):
    print(title)
    print(len(title) * char)


def infer_sorting(jobs) -> str | None:
    for option in SORT_OPTIONS:
        if option == "tick":
            found = sum(j.tick != 0 for j in jobs)
        else:
            found = sum((option in j.run_metrics) for j in jobs)

        if found > (len(jobs) // 2):
            return option

    return None


def main(args):
    setup_logging(debug=args.debug)

    log_parsing = LogParsing(parallel=args.parallel)
    if len(args.path) > 1:
        study = RollupStudy.from_files(args.path, parsed_logs=log_parsing)
    elif args.study or (args.path and args.path[0].endswith(".json")):
        study_json_path = args.study.name if args.study else args.path[0]
        study_directory = str(Path(study_json_path).absolute().parent)
        study = RollupStudy.from_json(
            args.study or args.path[0], study_directory=study_directory, parsed_logs=log_parsing
        )
    elif Path(args.path[0]).is_dir():
        study = RollupStudy.from_directory(args.path[0], parsed_logs=log_parsing)
    else:
        study = RollupStudy.from_files(list(glob(args.path[0])), parsed_logs=log_parsing)

    def bucket(failed_jobs, jobs, group=None):
        if args.summarize:
            total = len(jobs)
            passed = sum(job.successful for job in jobs)
            failed = sum(job.failed for job in jobs)
            incomplete = sum(job.incomplete for job in jobs)
            print(f"Passed: {passed} / {total} ({(passed / total):.2%})")
            if failed > 0:
                print(f"Failed: {failed} / {total} ({(failed / total):.2%})")
            if incomplete > 0:
                print(f"Incomplete: {incomplete} / {total} ({(incomplete / total):.2%})")
            print()

        if args.format == "CSV":
            jobs_by_tick = sorted(failed_jobs, key=lambda job: job.tick)
            for job in jobs_by_tick:
                columns = [
                    job.tick,
                    Bucketer.job_error_message(job),
                    " ".join([shlex.quote(word) for word in job.cmds[0]]),
                ]
                if group:
                    columns.insert(0, group)
                error_writer.writerow(columns)
        else:
            if failed_jobs:
                if not args.summarize:
                    logging.error(f"{len(failed_jobs)} jobs have errors.")
                bucketer = Bucketer()
                bucketer.bucket_jobs(failed_jobs, sort_by=args.sort_jobs_by)
                if args.format == "table":
                    bucketer.print_summary_table(jobs=args.jobs_per_bucket, line_length=args.line_length)
                else:
                    bucketer.print_summary(jobs=args.jobs_per_bucket)
            elif not args.summarize:
                logging.info("All jobs passed ")

    all_failed_jobs = list(study.failed_jobs())
    if args.jobs_per_bucket > 0 and args.sort_jobs_by == "infer":
        args.sort_jobs_by = infer_sorting(all_failed_jobs)

    all_jobs = list(study.all_jobs())
    if not args.ignore_incomplete:
        all_failed_jobs.extend(study.incomplete_jobs())

    if args.format == "CSV":
        if args.output != sys.stdout:
            args.output.truncate(0)
        error_writer = csv.writer(args.output)
        columns = ["tick", "error", "cmd"]
        if args.per:
            columns.insert(0, args.per)
        error_writer.writerow(columns)

    if not args.per:
        bucket(all_failed_jobs, all_jobs)
        return 0

    per_values = study.unique_meta_values(args.per)
    if not per_values:
        raise ValueError(f"No jobs found with --per {args.per}")

    for name in per_values:
        failed_jobs = [j for j in all_failed_jobs if j.meta.get(args.per) == name]
        if not failed_jobs and not args.summarize:
            continue

        if args.format != "CSV":
            header(name)

        jobs = [j for j in all_jobs if j.meta.get(args.per) == name]
        bucket(failed_jobs, jobs, group=name)
        if args.format != "CSV":
            print()

    return 0


if __name__ == "__main__":
    sys.exit(main(parse_args()))
