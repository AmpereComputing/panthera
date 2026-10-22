#!/usr/bin/env python3
# Copyright (c) 2020-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


import json
import sys

import numpy as np
import pandas as pd


def compute_conflicts(series, window):
    rolling = series.rolling(window).sum()
    counts = rolling.value_counts().sort_index()
    return counts.iloc[2:].sum()


def compute_total_conflicts(banks, times, num_banks):
    total_conflicts = 0
    for i in range(num_banks):
        s = pd.Series(np.where(banks == i, 1, 0), index=times)
        total_conflicts += compute_conflicts(s, "2000ns")
    return total_conflicts


def compute_bank_num(series, num_banks):
    return np.bitwise_and(np.right_shift(series, 6), num_banks - 1)


def filter_misses(df, hit_column):
    return df.groupby(hit_column).filter(lambda x: (x[hit_column] == 0).all())


def doit(cache, num_banks, df):
    hit = f"{cache[0]}.hit"
    address = cache[1]
    misses = filter_misses(df, hit)
    times = pd.to_datetime(misses["time"])
    bank = compute_bank_num(misses[address], num_banks)
    return compute_total_conflicts(bank, times, num_banks)


def hit_rate(cache, df):
    hit = f"{cache[0]}.hit"
    num_hits = df[hit].sum()
    num_accesses = df[hit].count()
    return num_hits / num_accesses


def main():
    input_file = sys.argv[1]
    output_file = sys.argv[2]

    caches = [("core.icf.cache", "program_counter")]

    df = pd.read_csv(input_file)

    to_sum = ["core.icf.cache.hit", "core.icf.cache.access"]

    stats = {}
    for stat in to_sum:
        stats[stat] = int(df[stat].sum())

    for cache in caches:
        bc = doit(cache, 4, df)
        stats[cache[0] + ".bank_conflicts"] = int(bc)

    with open(output_file, "w") as f:
        json.dump(stats, f, indent=2)


if __name__ == "__main__":
    main()
