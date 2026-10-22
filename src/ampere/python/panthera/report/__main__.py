#! /usr/bin/env python3
# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Generate a report from a CSV file.

The input to a report is always a CSV file. There are 2 kinds of reports.
One that processes a rollup.csv that comes from a Cheetah study.
The second that process a timeseries .ts.csv file.
"""

import logging
import os.path
import pkgutil
import sys

import pandas as pd

from panthera.report.helpers import ReportGenerator, create_argument_parser

from .. import install_dir, setup_logging


def find_reports():
    reports = {}
    reports_dir = os.path.join(install_dir, "lib", "python", "panthera", "report", "reports")
    module_iter = pkgutil.walk_packages([reports_dir])
    for module_info in module_iter:
        reports[module_info.name] = os.path.join(reports_dir, module_info.name + ".py")
    return reports


def main(argv):
    setup_logging()
    reports = find_reports()
    parser = create_argument_parser(reports)
    args = parser.parse_args(argv)

    for filename in args.input:
        logging.info("reading {}".format(filename))

        rg = ReportGenerator.from_args(filename, args, all_reports=reports)
        for report in rg.reports:
            rg.execute_report(report, {"format": args.format})


if __name__ == "__main__":
    main(sys.argv[1:])
