# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Helpful routines for rolling up SoC related data, like BW and latency.
"""

import glob
import os
import re

import pandas as pd


def rollup_latency(data_dir, **meta_data):
    """Rollup single-core latency run."""
    dfs = []
    file_re = re.compile(r".*latency_core([0-9]+)_(\S+)\\.summary\\.csv")

    for csv_file in passing_data_files(f"{data_dir}/**/latency*.summary.csv"):
        active_core, mem_size = file_re.match(csv_file).groups([1, 2])
        active_core = int(active_core)

        df = pd.read_csv(csv_file)
        df["core"] = active_core
        df["latency"] = df[f"inj{active_core}_latency"]
        df["mem_size"] = mem_size
        for k, v in meta_data.items():
            df[k] = v
        dfs.append(df[["core", "latency", "mem_size"] + list(meta_data.keys())])

    return pd.concat(dfs).set_index("core").sort_index()


def rollup_bandwidth(data_dir, **meta_data):
    """Rollup single-core BW runs."""
    dfs = []
    file_re = re.compile(r".*bw_core([0-9]+)_([^_]+)_(\S+)\\.summary\\.csv")

    for csv_file in passing_data_files(f"{data_dir}/**/bw*.summary.csv"):
        active_core, prog, mem_size = file_re.match(csv_file).groups([1, 2, 3])
        active_core = int(active_core)

        df = pd.read_csv(csv_file)
        df["core"] = active_core
        df["workload"] = prog
        df["mem_size"] = mem_size
        for k, v in meta_data.items():
            df[k] = v

        if prog == "read":
            df["bandwidth"] = 64 * calc_bw(df, "l2c_refill")
        elif prog == "write":
            df["bandwidth"] = 64 * calc_bw(df, "l2c_refill")
        elif prog == "copy":
            df["bandwidth"] = 64 * calc_bw(df, "l2c_refill")
        else:
            raise RuntimeError(f"unrecognized multiload program: {prog}")

        dfs.append(df[["core", "bandwidth", "workload", "mem_size"] + list(meta_data.keys())])

    return pd.concat(dfs).set_index("core").sort_index()


def rollup_multiload(data_dir, **meta_data):
    """Rollup BW and latency from a multiload run.

    This assumes that a CSV file was produced named as follows:

        multiload_lat<C>_agg<N>_<prog>_<size>

    where

        C    = latency core number
        N    = number of aggressor cores
        prog = aggressor program (e.g., "read")
        size = size of memory region for aggressors
    """

    dfs = []
    file_re = re.compile(r".*multiload_lat([0-9]+)_agg([0-9]+)_([^_]+)_(\S+).summary.csv")

    for csv_file in passing_data_files(f"{data_dir}/**/multiload*.summary.csv"):
        try:
            latcore, nagg, prog, mem_size = file_re.match(csv_file).groups([1, 2, 3, 4])
            latcore = int(latcore)
            nagg = int(nagg)
            ncores = nagg + 1

            df = pd.read_csv(csv_file)
            df["core"] = latcore
            df["ncores"] = ncores
            df["workload"] = prog
            df["mem_size"] = mem_size
            df["latency"] = calc_lat(df, latcore)
            for k, v in meta_data.items():
                df[k] = v

            if prog == "read":
                df["bandwidth"] = 64 * calc_bw(df, "l2c_refill")
            elif prog == "write":
                df["bandwidth"] = 64 * calc_bw(df, "l2c_refill")
            elif prog == "copy":
                df["bandwidth"] = 64 * calc_bw(df, "l2c_refill")
            else:
                raise RuntimeError(f"unrecognized multiload program: {prog}")
        except:
            raise RuntimeError(f"failed to roll up {csv_file}")

        dfs.append(df[["core", "ncores", "workload", "mem_size", "bandwidth", "latency"] + list(meta_data.keys())])

    return pd.concat(dfs).set_index("ncores").sort_index()


def rollup_bb(data_dir, **meta_data):
    """Rollup BW and latency from a BadgerBench (similar to multiload) run.

    This assumes that a CSV file was produced named as follows:

        bb_lat<C>_<N>_<size>

    where

        C    = latency core number
        N    = interval between memory ops (# of dependent adds; should basically be cycles)
        size = size of memory region for aggressors
    """

    dfs = []
    file_re = re.compile(f"{data_dir}/(.*)/bb_lat([0-9]+)_([0-9]+)_([^_]+)_(\\S+).summary.csv")
    nfiles = 0

    for csv_file in passing_data_files(f"{data_dir}/**/bb*.summary.csv"):
        print(csv_file)
        try:
            directory, latcore, interval, workload, mem_size = file_re.match(csv_file).groups([1, 2, 3, 4, 5])
            latcore = int(latcore)
            interval = int(interval)

            df = pd.read_csv(csv_file)
            df["core"] = latcore
            df["interval"] = interval
            df["directory"] = directory
            df["workload"] = workload
            df["mem_size"] = mem_size
            df["latency"] = calc_lat(df, latcore)

            df["bandwidth"] = 64 * calc_bw(df, "l2c_refill", "l2c_writeback")

            for k, v in meta_data.items():
                df[k] = v

            nfiles += 1
        except:
            raise RuntimeError(f"failed to roll up {csv_file}")

        dfs.append(
            df[
                ["core", "interval", "directory", "workload", "mem_size", "bandwidth", "latency"]
                + list(meta_data.keys())
            ]
        )

    if nfiles == 0:
        raise RuntimeError(f"found no passing runs in {data_dir}")

    return pd.concat(dfs).set_index("interval").sort_index()


def rollup_c2c_latency(data_dir, **meta_data):
    """Rollup core to core latency run."""
    file_re = re.compile(".*c2c_latency_([0-9]+)_([0-9]+)\\.summary\\.csv")

    mat = [[0 for col in range(128)] for row in range(128)]
    for csv_file in passing_data_files(f"{data_dir}/**/c2c_latency*.summary.csv"):
        core1, core2 = file_re.match(csv_file).groups([1, 2])
        core1 = int(core1)
        core2 = int(core2)
        df = pd.read_csv(csv_file)
        mat[core1][core2] = df.iloc[0][f"inj{core1}_latency"]
        mat[core2][core1] = df.iloc[0][f"inj{core2}_latency"]

    return pd.DataFrame(mat)


def rollup_c2c_ld_latency(data_dir, **meta_data):
    """Rollup core to core latency run."""
    file_re = re.compile(".*c2c_ld_latency_([0-9]+)_([0-9]+)\\.summary\\.csv")

    mat = [[0 for col in range(128)] for row in range(128)]
    for csv_file in passing_data_files(f"{data_dir}/**/c2c_ld_latency*.summary.csv"):
        core1, core2 = file_re.match(csv_file).groups([1, 2])
        core1 = int(core1)
        core2 = int(core2)
        df = pd.read_csv(csv_file)
        mat[core1][core2] = df.iloc[0][f"inj{core1}_latency"]
        mat[core2][core1] = df.iloc[0][f"inj{core2}_latency"]

    return pd.DataFrame(mat)


def rollup_c2c_bw(data_dir, **meta_data):
    """Rollup core to core bw run."""
    file_re = re.compile(".*c2c_bw_([0-9]+)_([0-9]+)\\.summary\\.csv")

    mat = [[0 for col in range(64)] for row in range(64)]
    for csv_file in passing_data_files(f"{data_dir}/**/c2c_bw*.summary.csv"):
        core1, core2 = file_re.match(csv_file).groups([1, 2])
        core1 = int(core1)
        core2 = int(core2)
        df = pd.read_csv(csv_file)
        mat[core1][core2] = df.iloc[0][f"inj{core1}_bw"]
        mat[core2][core1] = df.iloc[0][f"inj{core2}_bw"]

    return pd.DataFrame(mat)


def calc_bw(df, *bytes_cols):
    time_ns = (df["stop_time"] - df["start_time"]) / 1000.0
    bytes = sum(df[col].fillna(0) if col in df else 0 for col in bytes_cols)
    return bytes / time_ns


def calc_lat(df, latcore):
    return df[f"round_trip{latcore}.sum_latency"] / df[f"round_trip{latcore}.count"] / 1000.0


def passing_data_files(pattern):
    for csv_file in glob.glob(pattern, recursive=True):
        logfile = data_file_to_log_file(csv_file)
        if logfile is None or passed(logfile):
            yield csv_file


def data_file_to_log_file(filename):
    while "." in filename:
        basename, ext = os.path.splitext(filename)
        if os.path.exists(basename + ".log"):
            return basename + ".log"
        filename = basename
    # raise RuntimeError(f'could not file logfile corresponding to {filename}')
    return None


error_re = re.compile("^(Error.*)|^(Fatal.*)", re.I)


def passed(logfile):
    for line in open(logfile).readlines():
        if error_re.match(line):
            return False
    return True
