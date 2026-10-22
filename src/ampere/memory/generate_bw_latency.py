#!/usr/bin/env python3
# Copyright (c) 2022-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


import argparse
import json
import sys

import pandas as pd

parser = argparse.ArgumentParser(description="Generate JSON for memory model")
parser.add_argument("input", help="input JSON file")
parser.add_argument("output", type=argparse.FileType("w"), help="output JSON file")
parser.add_argument("--latency_offset", type=int, default=72, help="nanoseconds to subtract from input memory latency")
parser.add_argument("--num_cores", type=float, default=40.0, help="number of cores in simulated model")
parser.add_argument("--num_mcus", type=float, default=4.0, help="number of memory controllers in simulated model")

args = parser.parse_args()

df = pd.read_csv(args.input)

scale_factor = args.num_cores * args.num_mcus
print(scale_factor)

write_percentages = [0.10, 0.45, 1.0]

for i in [0, 2, 4]:
    df.iloc[:, i] = df.iloc[:, i].div(scale_factor)
    df.iloc[:, i + 1] = df.iloc[:, i + 1].sub(args.latency_offset)

curves = [{"write_percentage": wp, "bw_latency": []} for wp in write_percentages]

for index, row in df.iterrows():
    for i in range(3):
        bandwidth = row.iloc[i * 2]
        latency = row.iloc[(i * 2) + 1]
        curves[i]["bw_latency"].append([float(f"{bandwidth:.5f}"), float(f"{latency:.2f}")])

json.dump(curves, args.output, indent=None)
