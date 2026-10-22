#!/usr/bin/env python3
# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Concatenate multiple rollup CSV files into a single CSV.

The script is designed to read multiple CSV files from separate cheetah
studies and combine them into a single CSV.

Each of the input CSV files need to come from different directories.
The directory name is used as "configuration" in the output CSV file.
"""

import argparse
import logging
import os.path

import pandas as pd

from panthera import setup_logging


def rollup(files):
    dfs = []
    for f in files:
        configuration = os.path.dirname(f)
        logging.info("reading {}".format(f))
        df = pd.read_csv(f)
        df["configuration"] = configuration + "." + df["configuration"]
        dfs.append(df)

    # Pandas versions >= 0.23.0 add the 'sort' parameter and the current
    # behavior of sorting by default is deprecated and will be removed in the
    # future. First attempt the mew method before falling back on the < 0.23.0
    # usage.
    # See: https://pandas.pydata.org/pandas-docs/stable/reference/api/pandas.concat.html
    try:
        return pd.concat(dfs, sort=True)
    except TypeError:
        return pd.concat(dfs)


class MyArgumentParser(argparse.ArgumentParser):
    def convert_arg_line_to_args(self, arg_line):
        return arg_line.split()


if __name__ == "__main__":
    parser = MyArgumentParser(description=__doc__, fromfile_prefix_chars="@")

    parser.add_argument("inputs", nargs="+", help="input CSV files")
    parser.add_argument("-o", "--output", default="rollup.csv", help="output file")

    setup_logging()

    args = parser.parse_args()
    df = rollup(args.inputs)

    logging.info("writing {}".format(args.output))
    df.to_csv(args.output, index=False)
