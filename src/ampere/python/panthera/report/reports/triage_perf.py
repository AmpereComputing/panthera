#!/usr/bin/env python3
# Copyright (c) 2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Report performance outliers locally."""

import json
import logging
import re
from math import isclose

import pandas as pd
from panthera import ArgumentParser, setup_logging
from panthera.report.helpers import CONFIGURATION, infer_index, speedup
from panthera.report.notify import PrintNotifier
from scipy.stats import gmean


def parse_args():
    parser = ArgumentParser(description=__doc__, tool="report.triage_perf")

    parser.add_argument("data", help="CSV path")
    parser.add_argument("--title", default="Panthera performance triage", help="Report title")
    parser.add_argument("--template", default="triage_perf.md.j2", help="Report template")
    parser.add_argument("--path", help="Data path")
    parser.add_argument("--scurve-path", help="SCurve CSV path")
    parser.add_argument(
        "--difference",
        action="store_true",
        help="Use absolute difference instead of absolute ratio",
    )
    parser.add_argument("--exclude-unmatched", action="store_true", help="Exclude unmatched traces")
    parser.add_argument(
        "--speculation-threshold",
        type=int,
        help="Exclude traces with speculation above this",
    )
    parser.add_argument(
        "--error-threshold",
        default=0.1,
        type=float,
        help="Speedup error threshold for triage",
    )
    parser.add_argument(
        "--absolute-threshold",
        default=None,
        type=int,
        help="Absolute threshold metrics must be above to participate in triage",
    )
    parser.add_argument(
        "--median-threshold-ratio",
        default=None,
        type=float,
        help="Minimum threshold from ratio of median for metric to partitipate in triage",
    )
    parser.add_argument(
        "--max-threshold-ratio",
        default=None,
        type=float,
        help="Minimum threshold from ratio of max for metric to partitipate in triage",
    )
    parser.add_argument(
        "--acceptance-absolute-geomean",
        type=float,
        help="Exit if absolute geomean is met",
    )
    parser.add_argument(
        "--acceptance-within-threshold",
        type=float,
        help="Exit if ratio of traces are within the error-threshold",
    )
    parser.add_argument("--max-traces", type=int, default=20, help="Max traces to triage")
    parser.add_argument("--unit-metrics", help="JSON file of metrics within each unit")
    parser.add_argument(
        "--cycles-metric",
        default="sys.soc_0.core_0.pmu.cpu_cycles",
        help="Cycles metric",
    )
    parser.add_argument("--ignore", help="JSON file of traces to ignore")

    return parser.parse_args()


def load_study(path):
    return pd.read_csv(path)


def load_ignore(path):
    with open(path, "r") as jf:
        ignore_json = json.load(jf)

    traces = {}
    if isinstance(ignore_json, list):
        for ignore in ignore_json:
            traces.update(ignore["traces"])
    elif isinstance(ignore_json, dict):
        traces = ignore_json
    else:
        raise ValueError("Unexpected ignore JSON format")

    return traces


def normalize_and_ratio(
    df,
    metric,
    cycles_metric,
    ratio=True,
    absolute_threshold=None,
    median_threshold_ratio=None,
    max_threshold_ratio=None,
):
    index = infer_index(df)
    data = df.set_index([CONFIGURATION, index])

    def exclude_metric_below_threshold(data, metric, threshold):
        outside = (data[metric] < threshold).unstack(CONFIGURATION).all(axis=1)
        if outside.any():
            data = data.reset_index(level=CONFIGURATION)
            data.loc[outside, metric] = 1
            data = data.reset_index().set_index([CONFIGURATION, index])

        return data

    if metric != cycles_metric:
        try:
            variance = data[metric].unstack(CONFIGURATION).var().mean()
            # Replace zeros with 1 to see large differences
            data[metric] = data[metric].replace(0, 1)
            # Essentially exclude trace if all config values are below some absolute threshold for a trace
            if absolute_threshold is not None:
                data = exclude_metric_below_threshold(data, metric, absolute_threshold)
            # Essentially exclude trace if all config values are below some threshold relative to median
            if median_threshold_ratio is not None:
                median_threshold = data[metric].median() * median_threshold_ratio
                data = exclude_metric_below_threshold(data, metric, median_threshold)
            # Essentially exclude trace if all config values are below some threshold relative to max
            if max_threshold_ratio is not None:
                max_threshold = data[metric].max() * max_threshold_ratio
                data = exclude_metric_below_threshold(data, metric, max_threshold)
            # Normalize
            data[metric] = data[metric] / variance
        except KeyError:
            logging.warning(f"{metric} was missing")

    if ratio or metric == cycles_metric:
        data = data.reset_index()
        metric_ratio = speedup(data, metric)
        return metric_ratio[metric_ratio.columns[-1]]

    data = data[metric].unstack(CONFIGURATION)
    return abs(data[data.columns[0]] - data[data.columns[-1]])


def error(ratio_value):
    return 1 / ratio_value if (ratio_value != 0 and ratio_value < 1.0) else ratio_value


def metric_to_unit(metric, units):
    for unit, metrics in units.items():
        if metric in metrics:
            return unit
    return None


def main(args):
    setup_logging()
    data = load_study(args.data)
    ignore = load_ignore(args.ignore) if args.ignore else {}

    units = {}
    if args.unit_metrics:
        with open(args.unit_metrics, "r") as jf:
            units = json.load(jf)

    metrics = [args.cycles_metric]
    all_unit_metrics = []
    for unit_metrics in units.values():
        all_unit_metrics.extend(unit_metrics)
    metrics.extend(all_unit_metrics)

    # Calculate metric ratios
    trace_data = pd.concat(
        {
            metric: normalize_and_ratio(
                data,
                metric,
                args.cycles_metric,
                ratio=not args.difference,
                absolute_threshold=args.absolute_threshold,
                median_threshold_ratio=args.median_threshold_ratio,
                max_threshold_ratio=args.max_threshold_ratio,
            )
            for metric in metrics
        },
        axis=1,
    )

    index = infer_index(data)
    total_traces = trace_data.shape[0]
    if args.scurve_path:
        scurve_data = (
            load_study(args.scurve_path).set_index(index).rename(columns=lambda c: re.sub(r" ?[?#/]", "", c).lower())
        )
        trace_data = pd.concat([trace_data, scurve_data], axis=1)

    geomean = gmean(trace_data[args.cycles_metric].dropna())
    absolute_geomean = gmean(trace_data[args.cycles_metric].dropna().apply(error))

    if args.exclude_unmatched:
        trace_data = trace_data[trace_data["is_same_code"] == "Matched"]
    if args.speculation_threshold:
        trace_data = trace_data[trace_data["speculative"] < args.speculation_threshold]

    traces = (
        trace_data.dropna(how="all")
        .reset_index()
        .rename(
            columns={
                index: "name",
                "sys.soc_0.core_0.pmu.cpu_cycles": "speedup",
            }
        )
        .sort_values(by="speedup")
    )
    all_traces = traces.sort_values(by="name")

    if ignore:
        traces = traces[
            traces.apply(
                lambda r: r["name"] not in ignore or not isclose(r["speedup"], ignore[r["name"]], rel_tol=0.05),
                axis=1,
            )
        ]

    filtered_traces = traces.shape[0]
    filtered_geomean = gmean(traces["speedup"])
    filtered_absolute_geomean = gmean(traces["speedup"].apply(error))

    # Filter traces outside of threshold
    within_threshold = None
    for step in range(10):
        multiplier = 1 + step / 10
        within = traces["speedup"].between(
            1 - args.error_threshold * multiplier,
            1 + args.error_threshold * multiplier,
        )
        if within_threshold is None:
            within_threshold = within.sum()
        traces = traces[~within]
        if traces.shape[0] <= args.max_traces:
            break

    if traces.shape[0] > args.max_traces:
        traces = pd.concat(
            [
                traces.iloc[0 : args.max_traces // 2],
                traces.iloc[-args.max_traces // 2 :],
            ],
            axis=0,
        )

    if traces.empty:
        logging.info("No traces outside threshold")
        return

    acceptable_within_threshold = (
        not args.acceptance_within_threshold or (within_threshold / filtered_traces) >= args.acceptance_within_threshold
    )
    acceptable_absolute_geomean = (
        not args.acceptance_absolute_geomean or filtered_absolute_geomean <= args.acceptance_absolute_geomean
    )

    if (
        (args.acceptance_within_threshold or args.acceptance_absolute_geomean)
        and acceptable_within_threshold
        and acceptable_absolute_geomean
    ):
        logging.info("Acceptance criteria met")
        return

    # Calculate the worst-correlating metric for each trace.
    if all_unit_metrics:
        traces["worst"] = (traces[all_unit_metrics] if args.difference else traces[all_unit_metrics].map(error)).idxmax(
            axis=1
        )
    else:
        traces["worst"] = args.cycles_metric
    traces["unit"] = traces["worst"].apply(lambda x: metric_to_unit(x, units))
    traces["metric"] = traces["worst"].apply(lambda x: x.replace("sys.soc_0.core_0.", ""))

    renderer = PrintNotifier(args.template)
    content = renderer.render(
        title=args.title,
        data_path=args.path,
        total_traces=total_traces,
        filtered_traces=filtered_traces,
        threshold=args.error_threshold,
        within_threshold=within_threshold / total_traces,
        filtered_within_threshold=within_threshold / filtered_traces,
        geomean=geomean,
        filtered_geomean=filtered_geomean,
        absolute_geomean=absolute_geomean,
        filtered_absolute_geomean=filtered_absolute_geomean,
        traces=traces.to_dict("records"),
        all_traces=all_traces.to_dict("records"),
    )
    renderer.notify(content)


if __name__ == "__main__":
    main(parse_args())
