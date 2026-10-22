#!/bin/env python3
# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Rollup Domino data from a Domino study.
"""

import argparse
import os
import re
import sys

import pandas as pd

from panthera.matrix.data import Data

parser = argparse.ArgumentParser(description="Rollup a Domino Study")


def main():
    args = parse_args()
    df = Data(args.study_dir).get("summary")
    do_rollup(args, df)


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("study_dir", help="directory containing domino study")
    parser.add_argument("cfg", help="configuration")
    parser.add_argument("-o", "--output", help="output directory")
    args = parser.parse_args()
    if not args.output:
        args.output = args.study_dir
    return args


def rollup_latency(path, df):
    """Rollup single-core latency run."""
    dfs = []
    inj_re = re.compile("inj([0-9]+)_latency")
    lat_df = df[df["experiment"] == "latency"]
    lat_df = lat_df.assign(latency=lat_df.apply(assign_lat, axis=1, args=("active_core",)))
    return lat_df[["active_core", "mem_size", "latency"]].set_index("active_core").sort_index()


def rollup_bandwidth(path, df):
    """Rollup single-core bandwidth run."""
    bw_df = df[df["experiment"] == "bw"]
    bw_df = bw_df.assign(bandwidth=bw_df.apply(assign_bw, axis=1))
    return bw_df[["active_core", "workload", "mem_size", "bandwidth"]].set_index("active_core").sort_index()


def rollup_multiload(path, df):
    m_df = df[df["experiment"] == "multiload"]
    m_df = m_df.assign(latency=m_df.apply(assign_lat, axis=1, args=("latency_core",)))
    m_df = m_df.assign(bandwidth=m_df.apply(assign_bw, axis=1))
    return m_df[["latency_core", "workload", "agg", "mem_size", "latency", "bandwidth"]].set_index("agg").sort_index()


def rollup_bb(path, df):
    bb_df = df[df["experiment"] == "bb"]
    bb_df = bb_df.assign(latency=bb_df.apply(assign_lat, axis=1, args=("latency_core",)))
    bb_df = bb_df.assign(bandwidth=bb_df.apply(assign_bw, axis=1))
    return (
        bb_df[["latency_core", "workload", "interval", "mem_size", "latency", "bandwidth"]]
        .set_index("interval")
        .sort_index()
    )


def rollup_multicore(path, df):
    multicore_df = df[df["experiment"] == "multicore"]
    multicore_df = multicore_df.assign(bandwidth=multicore_df.apply(assign_core_bw, axis=1))
    return multicore_df[["workload", "mem_size", "bandwidth"]].set_index("mem_size").sort_index()


def rollup_ior_lat(path, df):
    """Rollup single-ior latency run."""
    dfs = []
    inj_re = re.compile("inj([0-9]+)_read_latency")
    lat_df = df[df["experiment"] == "ior_lat"]
    lat_df = lat_df.assign(latency=lat_df.apply(assign_ior_lat, axis=1, args=("active_ior",)))
    return lat_df[["active_ior", "latency"]].set_index("active_ior").sort_index()


def rollup_ior_ld_lat(path, df):
    """Rollup single-ior latency run."""
    dfs = []
    inj_re = re.compile("inj([0-9]+)_read_latency")
    lat_df = df[df["experiment"] == "ior_ld_lat"]
    lat_df = lat_df.assign(latency=lat_df.apply(assign_ior_lat, axis=1, args=("active_ior",)))
    return lat_df[["active_ior", "interval", "workload", "latency"]].set_index("active_ior").sort_index()


def rollup_ior_bw(path, df):
    """Rollup single-ior bandwidth run."""
    bw_df = df[df["experiment"] == "ior_bw"]
    bw_df = bw_df.assign(rd_bw=bw_df.apply(assign_rd_bw, axis=1))
    bw_df = bw_df.assign(wr_bw=bw_df.apply(assign_wr_bw, axis=1))
    return bw_df[["active_ior", "workload", "mem_size", "rd_bw", "wr_bw"]].set_index("active_ior").sort_index()


def rollup_ior_rwbw(path, df):
    """Rollup single-ior bandwidth run."""
    bw_df = df[df["experiment"] == "ior_rwbw"]
    bw_df = bw_df.assign(rd_bw=bw_df.apply(assign_rd_bw, axis=1))
    bw_df = bw_df.assign(wr_bw=bw_df.apply(assign_wr_bw, axis=1))
    return (
        bw_df[["active_ior", "workload", "tlp_size", "repeat", "rd_bw", "wr_bw", "axidelay", "iot_size"]]
        .set_index("active_ior")
        .sort_index()
    )


def rollup_multi_ior_bw(path, df):
    """Rollup single-ior bandwidth run."""
    bw_df = df[df["experiment"] == "multi_ior_bw"]
    bw_df = bw_df.assign(rd_bw=bw_df.apply(assign_rd_bw, axis=1))
    bw_df = bw_df.assign(wr_bw=bw_df.apply(assign_wr_bw, axis=1))
    return bw_df[["tlp_size", "iors", "rd_bw", "wr_bw"]].set_index("tlp_size").sort_index()


def rollup_mixed_ior_bw(path, df):
    """Rollup single-ior bandwidth run."""
    bw_df = df[df["experiment"] == "mixed_ior_bw"]
    bw_df = bw_df.assign(rd_bw=bw_df.apply(assign_rd_bw, axis=1))
    bw_df = bw_df.assign(wr_bw=bw_df.apply(assign_wr_bw, axis=1))
    return bw_df[["active_ior", "tlp_size", "tracker_size", "rd_bw", "wr_bw"]].set_index("active_ior").sort_index()


def assign_lat(row, key):
    latency_core = row[key]
    return row[f"inj{latency_core}_latency"]


def assign_ior_lat(row, key):
    latency_core = row[key]
    if f"inj{latency_core}_read_latency" in row:
        return row[f"inj{latency_core}_read_latency"]
    elif f"inj{latency_core}_write_latency" in row:
        return row[f"inj{latency_core}_write_latency"]


def assign_core_bw(row):
    return 64 * calc_bw(row, "l2c_refill", "l2c_writeback")


def assign_bw(row):
    if row["workload"] == "read":
        return 64 * calc_bw(row, "l2c_refill")
    elif row["workload"] == "write":
        return 64 * calc_bw(row, "l2c_writeback")
    else:
        return 64 * calc_bw(row, "mem_send_dat", "mem_recv_dat")


def assign_rd_bw(row):
    return 64 * calc_bw(row, "ior_refill")


def assign_wr_bw(row):
    return 64 * calc_bw(row, "mem_recv_dat")


def calc_bw(df, *bytes_cols):
    time_ns = (df["stop_time"] - df["start_time"]) / 1000.0
    bytes = sum(df[col] if col in df else 0 for col in bytes_cols)
    return bytes / time_ns


def do_rollup(args, df):
    if not os.path.exists(args.output):
        os.makedirs(args.output, exist_ok=True)

    path = os.path.join(args.study_dir, args.cfg, "experiment.latency")
    if os.path.exists(path):
        ndf = rollup_latency(path, df)
        ndf.to_csv(os.path.join(args.output, "latency.csv"))
    path = os.path.join(args.study_dir, args.cfg, "experiment.bw")
    if os.path.exists(path):
        ndf = rollup_bandwidth(path, df)
        ndf.to_csv(os.path.join(args.output, "bandwidth.csv"))

    path = os.path.join(args.study_dir, args.cfg, "experiment.multiload")
    if os.path.exists(path):
        ndf = rollup_multiload(path, df)
        ndf.to_csv(os.path.join(args.output, "multiload.csv"))

    path = os.path.join(args.study_dir, args.cfg, "experiment.bb")
    if os.path.exists(path):
        ndf = rollup_bb(path, df)
        ndf.to_csv(os.path.join(args.output, "bb.csv"))

    path = os.path.join(args.study_dir, args.cfg, "experiment.bb_half")
    if os.path.exists(path):
        ndf = rollup_bb(path, df)
        ndf.to_csv(os.path.join(args.output, "bb_half.csv"))

    path = os.path.join(args.study_dir, args.cfg, "experiment.multicore")
    if os.path.exists(path):
        ndf = rollup_multicore(path, df)
        ndf.to_csv(os.path.join(args.output, "multicore.csv"))

    path = os.path.join(args.study_dir, args.cfg, "experiment.ior_lat")
    if os.path.exists(path):
        ndf = rollup_ior_lat(path, df)
        ndf.to_csv(os.path.join(args.output, "ior_latency.csv"))

    path = os.path.join(args.study_dir, args.cfg, "experiment.ior_ld_lat")
    if os.path.exists(path):
        ndf = rollup_ior_ld_lat(path, df)
        ndf.to_csv(os.path.join(args.output, "ior_ld_latency.csv"))

    path = os.path.join(args.study_dir, args.cfg, "experiment.ior_bw")
    if os.path.exists(path):
        ndf = rollup_ior_bw(path, df)
        ndf.to_csv(os.path.join(args.output, "ior_bw.csv"))

    path = os.path.join(args.study_dir, args.cfg, "experiment.ior_rwbw")
    if os.path.exists(path):
        ndf = rollup_ior_rwbw(path, df)
        ndf.to_csv(os.path.join(args.output, "ior_rwbw.csv"))

    path = os.path.join(args.study_dir, args.cfg, "experiment.multi_ior_bw")
    if os.path.exists(path):
        ndf = rollup_multi_ior_bw(path, df)
        ndf.to_csv(os.path.join(args.output, "multi_ior_bw.csv"))

    path = os.path.join(args.study_dir, args.cfg, "experiment.mixed_ior_bw")
    if os.path.exists(path):
        ndf = rollup_mixed_ior_bw(path, df)
        ndf.to_csv(os.path.join(args.output, "mixed_ior_bw.csv"))


if __name__ == "__main__":
    main()
