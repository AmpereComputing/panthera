# Copyright (c) 2020-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Print stats from a timeseries."""

import argparse
import logging
import sys

import pandas as pd


def print_metrics(filename, metrics):
    logging.info(f"reading {filename}")
    df = pd.read_csv(filename)
    for metric in metrics:
        if metric in df:
            stat = df[metric].sum()
            print(f"{filename} {metric} {stat}")
        else:
            logging.warning(f"Couldn't find {metric} in {filename}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("timeseries", nargs="+", help="name of timeseries files")
    parser.add_argument("--metric", action="append", help="name of metric")

    args = parser.parse_args(sys.argv[1:])

    if args.metric and len(args.metric):
        for ts in args.timeseries:
            print_metrics(ts, args.metric)
