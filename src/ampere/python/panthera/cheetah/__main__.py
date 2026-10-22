#! /usr/bin/env python3
# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


"""
Run a set of panthera simulations

The set of simulations is created based on the configurations listed in the
specification file and the workloads in the workloads file.

By default the simulations are run using slurm. Use the --run option to
override this.

"""

import logging
import os
import sys

from panthera.cheetah import argument_parser, create_xml_report, run_study
from panthera.cheetah.bucketing import Bucketer
from panthera.cheetah.studies.cheetah import Cheetah


def parse_args(cheetah):
    parser = argument_parser(
        description=__doc__,
        prog=__file__,
    )

    cheetah.parser_add_args(parser)

    args = parser.parse_args()
    if args.study is None:
        specification_name = os.path.basename(args.specification)
        (root, _ext) = os.path.splitext(specification_name)
        args.study = root

    return args


def main():
    cheetah = Cheetah()
    args = parse_args(cheetah)
    study = run_study(args.study, cheetah.apply([vars(args)]), args)
    if args.dry_run:
        return 0

    result = study.as_rollup()
    result.parse_logs()

    # Cheetah always creates an XML report
    if not args.xml_report:
        report_name = os.path.join(result.name, "report.xml")
        create_xml_report(study, rollup=result, path=report_name)

    all_jobs = list(result.all_jobs())
    incomplete_jobs = list(result.incomplete_jobs())
    failed_jobs = list(result.failed_jobs())

    count_all = len(all_jobs)
    count_incomplete = len(incomplete_jobs)
    count_failed = len(failed_jobs)

    if count_incomplete > 0 or count_failed > 0:
        logging.error(f"ran {count_all} jobs, {count_incomplete} incomplete, {count_failed} failed ")
        bucketer = Bucketer()
        bucketer.bucket_jobs(failed_jobs)
        bucketer.print_summary(jobs=4)
        return 1
    else:
        logging.info(f"ran {count_all} jobs, all jobs passed ")
        return 0


sys.exit(main())
