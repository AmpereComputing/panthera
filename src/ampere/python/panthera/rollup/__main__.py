#! /usr/bin/env python3
# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Rollup simulation data for a cheetah study.

Read the summary and/or timeseries files for every job in the study, then
generate CSV files with metric formulas applied, and metrics aggregated by
trace, workload, and suite.
"""

import argparse
import logging
import os
import sys
from multiprocessing import cpu_count

import pandas as pd

from panthera import setup_logging
from panthera.cheetah.study import RollupStudy
from panthera.snippets.regr_job import RegrJob

from . import GROUPBY
from .rollup import Rollup
from .timeseries import TimeSeries


def write_csv(df, dirname, filename):
    csv_filename = os.path.join(dirname, filename)
    logging.info("writing {}".format(csv_filename))
    df.to_csv(csv_filename)


def write_excel(df, dirname, filename, by):
    excel_filename = os.path.join(dirname, filename)
    logging.info("writing {}".format(excel_filename))
    excel_writer = pd.ExcelWriter(excel_filename, engine="xlsxwriter")
    df.to_excel(excel_writer, sheet_name=f"by_{by}")
    excel_writer.close()


def create_argument_parser():
    parser = argparse.ArgumentParser(description=__doc__)

    input_group = parser.add_argument_group("Input")
    input_args = input_group.add_mutually_exclusive_group(required=True)
    input_args.add_argument("study", type=argparse.FileType("r"), nargs="?", help="study JSON file")
    input_args.add_argument(
        "--ts", "--timeseries", action="append", default=[], help="individual timeseries file(s) to roll up"
    )
    input_args.add_argument("--summary-csv", action="append", help="Summary CSV for trace")

    ts_group = parser.add_argument_group("Timeseries")
    ts_group.add_argument("--rollup-timeseries", action="store_true", help="apply formulas to timeseries")
    ts_group.add_argument(
        "--timeseries-outdir", default="inplace", help="directory for rolled up timeseries files (default inplace)"
    )

    processing_group = parser.add_argument_group("Processing")
    processing_group.add_argument("--formula", action="append", help="rollup formula file")
    processing_group.add_argument(
        "--keep-factored", action="store_true", help="Do not strip columns with names containing slashes (/)"
    )
    processing_group.add_argument(
        "--mean",
        default="geometric",
        choices=["geometric", "arithmetic"],
        help="which type of mean to use for suite level aggregation",
    )
    processing_group.add_argument(
        "--nofillna", dest="fillna", action="store_false", help="do not replace NaN values with zero."
    )
    processing_group.add_argument("--run-metrics", action="store_true", help="Insert runtime metrics from logfile")
    processing_group.add_argument("--study-list", help="Override traces/workloads with study list")

    output_group = parser.add_argument_group("Output")
    output_group.add_argument("--output", default="rollup.csv", help="output CSV file name")
    output_group.add_argument(
        "--outdir", default=None, help="directory name in which to place CSV file (defaults to name of study)"
    )
    output_group.add_argument("--groupby", action="append", choices=GROUPBY, help="how to aggregate data in output")
    output_group.add_argument("--excel", action="store_true", help="generate Excel output file")
    output_group.add_argument(
        "--separate-excel",
        action="store_true",
        help="generate separate Excel files for trace/workload/study, instead of csv",
    )

    parser.add_argument("--summary-suffix", help="Summary CSV file suffix")
    parser.add_argument("--timeseries-suffix", help="Timeseries CSV file suffix")
    parser.add_argument("--print-unfinished", action="store_true")
    parser.add_argument(
        "--threads", type=int, default=max(1, int(cpu_count() / 2)), help="Number of threads to use for rolling up"
    )
    parser.add_argument("-v", "--verbose", action="store_true")
    parser.add_argument("--debug", action="store_true", help="Enable debug logging")

    # This causes us to load a study using a special version of the Job class
    # specialized for RTL regression runs, but shouldn't generally be used
    parser.add_argument("--regr", action="store_true", help=argparse.SUPPRESS)

    return parser


def main(argv):
    parser = create_argument_parser()
    args = parser.parse_args(argv)
    (root, ext) = os.path.splitext(args.output)

    setup_logging(debug=args.debug)

    if args.study:
        logging.info("reading {}".format(args.study.name))
        if args.regr:
            study = RollupStudy.from_json(args.study, job_class=RegrJob)
        else:
            study = RollupStudy.from_json(args.study)
        rollup = Rollup.from_study(
            study,
            summary_suffix=args.summary_suffix,
            timeseries_suffix=args.timeseries_suffix,
            verbose=args.print_unfinished,
            timeseries=args.rollup_timeseries,
            run_metrics=args.run_metrics,
            study_list=args.study_list,
        )
    elif args.ts:
        rollup = Rollup()
        study = None
        for ts in args.ts:
            rollup.timeseries.append(TimeSeries(csv=ts))
    elif args.summary_csv:
        rollup = Rollup.from_summaries(list(args.summary_csv))
        study = None

    if args.formula:
        rollup.read_formula_files(args.formula)

    outdir = args.outdir
    if not outdir:
        outdir = study.name if study else "."

    if args.excel:
        # Create a Pandas Excel writer using XlsxWriter as the engine.
        excel_filename = os.path.join(outdir, f"{root}.xlsx")
        logging.info(f"writing Excel file {excel_filename}")
        excel_writer = pd.ExcelWriter(excel_filename, engine="xlsxwriter")

    groupby = args.groupby if args.groupby else GROUPBY if study else ["trace"]

    for by in groupby:
        logging.info(f"rolling up by {by}")
        df = rollup.rollup(by=by, mean=args.mean, workers=args.threads, fillna=args.fillna)

        if not args.keep_factored:
            noslash = ["/" not in name for name in df.columns.values]
            df = df.loc[:, noslash]

        write_csv(df, outdir, root + ".by_" + by + ext)
        if args.separate_excel:
            write_excel(df, outdir, f"{root}.by_{by}.xlsx", by)
        elif args.excel:
            df.to_excel(excel_writer, sheet_name=f"by_{by}")

    if args.excel:
        excel_writer.close()

    if args.formula and args.rollup_timeseries:
        rollup.rollup_timeseries(
            output_directory=args.timeseries_outdir, workers=args.threads, keep_factored=args.keep_factored
        )


if __name__ == "__main__":
    main(sys.argv[1:])
