#!/usr/bin/env python3
# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Compute drift between two different studies and return error if outside allowed amount."""

import logging
import sys
import warnings
from typing import List

import pandas as pd
from numpy import mean
from panthera import ArgumentParser, setup_logging
from panthera.rollup import CONFIGURATION
from scipy.stats import gmean


def read_study(filename: str, metrics: List[str] = None) -> pd.DataFrame:
    df = pd.read_csv(filename)
    df = df.set_index([df.columns[0], CONFIGURATION])
    if metrics:
        df = df[metrics]
        for metric in metrics:
            nan_total = df[metric].isna().sum()
            if nan_total > 0:
                logging.warning(f"{metric} in {filename} has {nan_total} NaNs")

        return df

    return df.select_dtypes(include="number")


def header(text: str) -> None:
    print(text)
    print("=" * len(text.strip()))


def parse_args():
    parser = ArgumentParser(description=__doc__, tool="report.check_drift")

    parser.add_argument("current", help="Current study CSV")
    parser.add_argument("previous", help="Previous study CSV")
    parser.add_argument(
        "--method",
        choices=["ratio", "diff", "absolute_ratio", "absolute_diff"],
        default="diff",
        help="Comparison function between studies",
    )
    parser.add_argument("--metric", action="append", help="Metrics to compare")
    parser.add_argument(
        "--allowed-drift", type=float, default=0, help="Allowed absolute drift/difference in values per trace/metric"
    )
    parser.add_argument(
        "--show-outliers", action="store_true", help="Show all outliers (traces which had drift outside allowed)"
    )
    parser.add_argument("--ignore-missing", action="store_true", help="Ignore missing traces/metrics")

    return parser.parse_args()


def main(args):
    setup_logging()

    def compare(first, second):
        if args.method == "ratio":
            return first / second
        elif args.method == "absolute_ratio":
            values = first / second
            values[values < 1] = 1 / values[values < 1]
            return values
        elif args.method == "diff":
            return first - second
        elif args.method == "absolute_diff":
            return abs(first - second)

    def normalize(values):
        if "ratio" in args.method:
            return values - 1
        return values

    def readable_drift(value):
        if "ratio" in args.method:
            return f"{value:0.4%}"
        return f"{value:0.4g}"

    agg_func = gmean if "ratio" in args.method else mean

    current = read_study(args.current, metrics=args.metric)
    previous = read_study(args.previous, metrics=args.metric)

    drift = compare(current, previous)

    missing = drift.isna().all(axis=1)
    total_missing = missing.sum()
    if total_missing > 0:
        if args.ignore_missing:
            logging.info(f"Ignoring {total_missing} traces")
            drift = drift[~missing]
        else:
            logging.warning(f"{total_missing} missing traces, aggregate may be NaN")

    outliers = abs(normalize(drift)) > args.allowed_drift

    total_drift = normalize(agg_func(drift.mean(axis=1)))

    with warnings.catch_warnings():
        warnings.filterwarnings("ignore", category=FutureWarning)
        drift_per_config = drift.groupby(CONFIGURATION).agg(agg_func)

    outliers_per_config = outliers.groupby(CONFIGURATION).sum().rename(lambda c: f"{c} outliers", axis=1)

    header("\nSummary:")
    if args.metric:
        print(pd.concat([drift_per_config, outliers_per_config], axis=1))
    else:
        print(
            pd.concat(
                {
                    "drift": drift_per_config.mean(axis=1),
                    "outliers": outliers.any(axis=1).groupby(CONFIGURATION).sum(),
                    "different metrics": (drift_per_config.fillna(0) != 0).T.sum(),
                },
                axis=1,
            )
        )

    if outliers.any().any():
        if not args.metric:
            header("\nMetric difference:")
            metric_drift = drift_per_config.mean(axis=0)
            print(metric_drift[metric_drift.fillna(0) != 0])

        all_outliers = drift[outliers.any(axis=1)]
        if args.metric:
            all_outliers = all_outliers.sort_values(args.metric[0])
        else:
            all_outliers = pd.DataFrame({"drift": all_outliers.mean(axis=1).sort_values()})

        print(f"\nMetric drift found of {readable_drift(total_drift)} on average, {all_outliers.shape[0]} outliers!")
        if args.show_outliers:
            header("\nAll outliers:")
            print(all_outliers)
            print()

        sys.exit(1)
    else:
        allowed = f" outside {args.allowed_drift}" if args.allowed_drift > 0 else ""
        print(f"\nNo metric drift{allowed} (on average {readable_drift(total_drift)})!")


if __name__ == "__main__":
    main(parse_args())
