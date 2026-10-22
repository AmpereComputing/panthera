#! /usr/bin/env python3
# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Create a list of instruction streams to run a relatively representation regression
from a trace list by taking the highest weight stream from each workload.

"""

import argparse
import json

import pandas as pd

parser = argparse.ArgumentParser(
    description="Create regression workloads from highest weight traces in a study tracelist."
)
parser.add_argument("tracelist", type=str, help="path to the input study tracelist")
parser.add_argument("regression", type=str, help="path to output the regression workloads")
args = parser.parse_args()

df = pd.read_json(args.tracelist)
df_max = df.loc[df.groupby("workload")["weight"].idxmax(), ["name", "file"]]
df_max["file"] = "/is/" + df_max["file"]

with open(args.regression, "w") as file:
    json.dump(json.loads(df_max.to_json(orient="records")), file, indent=4)
