#!/usr/bin/env python3
# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


import enum
import re

import numpy as np
import pandas as pd


class BranchType(enum.Enum):
    NONE = 0
    CONDITIONAL_DIRECT = 1
    UNCONDITIONAL_DIRECT = 2
    UNCONDITIONAL_INDIRECT = 3
    CALL_DIRECT = 4
    CALL_INDIRECT = 5
    RETURN = 6
    UNSPECIFIED = 7


def report(df, threshold=10, limit=10):
    if len(df.index) > 1:
        df = pd.DataFrame(data=df.sum().to_dict(), index=[0])
    pc_table = generate_pc_to_branch_type_table(df)
    prediction_table = generate_pc_to_prediction_table(df)
    df = prediction_table.merge(pc_table, left_index=True, right_index=True)
    df = df[df["mispredict"] > 0]
    df = df[df["occurrences"] > threshold].sort_values("mispredict rate", ascending=False)
    print_table(df, limit)


def generate_pc_to_prediction_table(df):
    """Turn a DataFrame from "wide" format with the per-pc prediction info in columns
    into one in "long" format with the pc as an index.
    """
    df["id"] = df.index  # For some reason wide_to_long only understands column names as identifiers
    pivot_columns = set()
    for c in df.columns:
        m = re.match(r"sys.soc_0.core_0.branch\.(mis)?predict:", c)
        if m:
            pivot_columns.add(m.group(0))
    df = pd.wide_to_long(df, pivot_columns, i="id", j="pc").reset_index(0)
    compute_prediction_columns(df)
    return df


def compute_prediction_columns(df):
    predict_columns = [c for c in df.columns if re.match(r"sys\.soc_0\.core_0\.branch\.predict:", c)]
    mispredict_columns = [c for c in df.columns if re.match(r"sys\.soc_0\.core_0\.branch\.mispredict:", c)]
    df["predict"] = sum([df[c].fillna(0) for c in predict_columns])
    df["mispredict"] = sum([df[c].fillna(0) for c in mispredict_columns])
    df["occurrences"] = df["predict"] + df["mispredict"]
    df["mispredict rate"] = df["mispredict"] / df["occurrences"]


def generate_pc_to_branch_type_table(df):
    df = df[[c for c in df.columns if "br_type:" in c]].copy()
    df["id"] = df.index
    df = pd.wide_to_long(
        df, set([m.group(1) for c in df.columns for m in [re.match(r"(.*/program_counter:)", c)] if m]), i="id", j="pc"
    )
    for c in df.columns:
        df[c] = process_type_column(df[c])
    df["branch type"] = df.agg(max, axis=1)
    return df[["branch type"]].reset_index(0, drop=True)


def process_type_column(column):
    branch_type = int(re.search(r"br_type:(\d+)", column.name).group(1))
    return column.apply(lambda x: 0 if np.isnan(x) else branch_type)


def print_table(df, limit):
    def percent(x):
        return f"{x * 100.0:.2f}%"

    def branch_type_name(x):
        return BranchType(x).name.lower().replace("_", " ")

    df = df.reset_index()
    df["pc"] = [f"0x{int(pc):x}" for pc in df["pc"]]
    df = df.set_index("pc")

    print(
        df.head(limit).to_string(
            columns=["predict", "mispredict", "mispredict rate", "branch type"],
            formatters={"mispredict rate": percent, "branch type": branch_type_name},
        )
    )


def main():
    args = parse_args()
    csv = load_csv(args.event_stream, args)
    report(pd.read_csv(io.StringIO(csv)), threshold=args.threshold, limit=args.limit)


def parse_args():
    parser = argparse.ArgumentParser(description="Generate misprediction info per PC")
    parser.add_argument(
        "--threshold",
        metavar="N",
        type=int,
        default=10,
        help="Discard branches with fewer than N occurrences (default: 10)",
    )
    parser.add_argument(
        "--limit",
        metavar="N",
        type=int,
        default=10,
        help="Show only the top N branches by mispredict rate (default: 10)",
    )
    parser.add_argument("--ts-start", help="when to start monitoring mispredictions")
    parser.add_argument("--ts-stop", help="when to stop monitoring mispredictions")
    parser.add_argument("event_stream", help="Event stream")
    return parser.parse_args()


def load_csv(event_stream, args):
    with tempfile.NamedTemporaryFile() as tmp:
        args = []
        if args.ts_start:
            cmd += ["--start", args.ts_start]
        if args.ts_stop:
            cmd += ["--stop", args.ts_stop]
        args += ["@mispredictions_by_pc.cfg", "+summarize", "--summary", tmp.name]
        evp.run(event_stream, args)
        return open(tmp.name).read()


if __name__ == "__main__":
    import argparse
    import io
    import tempfile

    import ampere.python.panthera.events.evp as evp

    main()
else:
    report(df, threshold=0, limit=None)
