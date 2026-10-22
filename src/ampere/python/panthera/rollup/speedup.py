#!/usr/bin/env python3
# Copyright (c) 2020-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Compute the speedup (ratio) of each configuration compared to the baseline."""

import logging
import os.path

import numpy as np
import pandas as pd

from panthera import ArgumentParser, setup_logging
from panthera.rollup import CONFIGURATION


def speedup(filename, basename):
    df = pd.read_csv(filename)

    index = df.columns[0]

    logging.info(f"aligning rows on {index}")

    grouped = df.groupby(CONFIGURATION, sort=False)

    # If no basename is supplied use the configuration name
    # of the first row in the dataframe.
    if basename is None:
        basename = df[CONFIGURATION][0]

    logging.info(f"using {basename} as baseline")

    base = grouped.get_group(basename)

    base_numeric = base.select_dtypes(include=np.number)
    base_text = base.select_dtypes(exclude=np.number)
    base_numeric.index = base[index]
    base_text.index = base[index]

    dfs = []
    for name, group in grouped:
        logging.info(f"computing speedup of {name} / {basename} ")
        group_numeric = group.select_dtypes(include=np.number)
        group_text = group.select_dtypes(exclude=np.number)
        group_numeric.index = group[index]
        group_text.index = group[index]
        sup = group_numeric.divide(base_numeric)
        group_result = pd.concat([sup, group_text], axis="columns")
        dfs.append(group_result)

    return pd.concat(dfs, axis="rows")


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__, tool="rollup.speedup")

    parser.add_argument("inputs", nargs="+", help="input CSV files")
    parser.add_argument("--base", help="name of baseline configuration")

    setup_logging()

    args = parser.parse_args()
    for i in args.inputs:
        (path, name) = os.path.split(i)
        out = os.path.join(path, f"speedup.{name}")
        df = speedup(i, basename=args.base)
        logging.info(f"writing {out}")
        df.to_csv(out)
