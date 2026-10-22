# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Conversion to OpenMetrics format for CI tracking."""

import os

import pandas as pd
from panthera.report.helpers import ReportGenerator, create_argument_parser
from panthera.rollup import CONFIGURATION


def parse_args():
    parser = create_argument_parser(description=__doc__, tool="openmetrics")
    parser.add_argument("-c", "--config", action="append", help="Config to include")
    parser.add_argument("-m", "--metric", action="append", help="Metric to include")
    parser.add_argument(
        "--metric-type",
        action="append",
        nargs=2,
        metavar=("NAME", "TYPE"),
        help="Metric and associated type",
    )

    return parser.parse_args()


def infer_metric_type(name):
    return "guage"


def main(output_dir, df, args=None):
    output = os.path.join(output_dir, "metrics.txt")

    metrics = default_metrics = {"sys.soc_0.core_0.ipc": "gauge"}
    if args is not None:
        metrics = {name: infer_metric_type(name) for name in args.metric}
        if args.metric_type:
            metrics.update({name: mtype for name, mtype in args.metric_type.items()})
        if not metrics:
            metrics = default_metrics

        if args.config:
            df = df.loc[list(args.config)]

    aggregated = df[[CONFIGURATION] + list(metrics.keys())].groupby(CONFIGURATION).mean()

    def normalize_metric(key):
        return key.lower().replace("sys.soc_0.core_0.", "").replace("-", "_").replace(".", "_")

    with open(output, "w") as of:
        for metric, row in aggregated.T.iterrows():
            metric_type = metrics.get(metric) or "counter"
            metric = normalize_metric(metric)
            of.write(f"# TYPE {metric} {metric_type}\n")

            for configuration, value in row.items():
                full_metric = metric + '{configuration="' + configuration + '"}'
                of.write(f"{full_metric} {value}\n")

            of.write("\n")


if __name__ == "__main__":
    args = parse_args()
    report = ReportGenerator.from_args(args.input[0], args)
    main(report.output_dir, report.df, args)
else:
    main(output_dir, df)
