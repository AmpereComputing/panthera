#!/bin/env python3
# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Rollup Domino data from a Domino study.
"""

import argparse
import logging
import os

import panthera.rollup.soc as rollup

log = logging.getLogger(__name__)


def main():
    args = parse_args()
    do_rollup(args)


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("study_dir", help="directory containing domino study")
    parser.add_argument("-o", "--output", help="output directory")
    args = parser.parse_args()
    if not args.output:
        args.output = args.study_dir
    return args


def do_rollup(args):
    if not os.path.exists(args.output):
        os.makedirs(args.output, exist_ok=True)

    path = os.path.join(args.study_dir, "latency")
    if os.path.exists(path):
        df = rollup.rollup_latency(path)
        df.to_csv(os.path.join(args.output, "latency.csv"))

    path = os.path.join(args.study_dir, "bw")
    if os.path.exists(path):
        df = rollup.rollup_bandwidth(path)
        df.to_csv(os.path.join(args.output, "bandwidth.csv"))

    path = os.path.join(args.study_dir, "multiload")
    if os.path.exists(path):
        df = rollup.rollup_multiload(path)
        df.to_csv(os.path.join(args.output, "multiload.csv"))

    path = os.path.join(args.study_dir, "bb")
    if os.path.exists(path):
        df = rollup.rollup_bb(path)
        df.to_csv(os.path.join(args.output, "bb.csv"))

    path = os.path.join(args.study_dir, "bb_half")
    if os.path.exists(path):
        df = rollup.rollup_bb(path)
        df.to_csv(os.path.join(args.output, "bb_half.csv"))

    path = os.path.join(args.study_dir, "c2c_latency")
    if os.path.exists(path):
        df = rollup.rollup_c2c_latency(path)
        df.to_csv(os.path.join(args.output, "c2c_latency.csv"))

    path = os.path.join(args.study_dir, "c2c_ld_latency")
    if os.path.exists(path):
        df = rollup.rollup_c2c_ld_latency(path)
        df.to_csv(os.path.join(args.output, "c2c_ld_latency.csv"))

    path = os.path.join(args.study_dir, "c2c_bw")
    if os.path.exists(path):
        df = rollup.rollup_c2c_bw(path)
        df.to_csv(os.path.join(args.output, "c2c_bw.csv"))


if __name__ == "__main__":
    main()
