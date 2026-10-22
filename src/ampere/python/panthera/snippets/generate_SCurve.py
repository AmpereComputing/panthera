#!/usr/bin/env python3
# Copyright (c) 2023-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Creates IPC-Scurve for two rollups as part of PV.
"""

import json
import logging
import os
import statistics
import subprocess
from concurrent.futures import ProcessPoolExecutor, as_completed
from math import isclose
from multiprocessing import cpu_count

import pandas as pd

from .. import ArgumentParser, install_dir, setup_logging

TEMPORARY_METRICS = ("warmup_instructions", "measurement_instructions")


def parse_args():
    parser = ArgumentParser(description=__doc__, tool="generate_SCurve")
    parser.add_argument("output_SCurve_csv", help="Output S-curve between RTL IPC and Panthera IPC")
    parser.add_argument("Panthera_rollup_csv", help="This the csv file that IPC for the Panthera run")
    parser.add_argument("RTL_rollup_csv", help="This the csv file that IPC for the RTL run")
    parser.add_argument(
        "temp_output_folder",
        help="This folder will have the temp files like speculations stats and es_same check, if not provided it will be created",
    )
    parser.add_argument(
        "--RTL_regr",
        help="we use this eventstream files for getting the speculation numbers",
    )
    parser.add_argument(
        "--panthera_es_folder",
        help="we use this eventstream files for getting the data from es",
    )
    parser.add_argument(
        "--traces_to_debug",
        default="traces_to_debug.txt",
        help="This list file contains the traces to debug",
    )
    parser.add_argument(
        "--es1_es_same_inum_start",
        default=100,
        type=int,
        help="This will set the start inum used to mismatch check",
    )
    parser.add_argument(
        "--es2_es_same_inum_start",
        default=100,
        type=int,
        help="This will set the start inum used to mismatch check",
    )
    parser.add_argument(
        "--max_instructions",
        default=30000,
        type=int,
        help="This will set the start inum used to mismatch check",
    )
    parser.add_argument(
        "--mismatch_check",
        action="store_true",
        help="If this flag set to 1, we compare the two codes to make sure they are the same",
    )
    parser.add_argument(
        "--RTL_speculation_check",
        action="store_true",
        help="If this flag set to 1, we extract and report the speculation occurences in RTL",
    )
    parser.add_argument(
        "--lower_threshold",
        default=0.90,
        type=float,
        help="This is the lower threshold for selecting traces to debug",
    )
    parser.add_argument(
        "--upper_threshold",
        default=1.10,
        type=float,
        help="This is the upper threshold for selecting the traces to debug",
    )
    parser.add_argument("--ignore", help="JSON file of traces to ignore")
    parser.add_argument(
        "-j",
        "--parallel",
        metavar="N",
        default=max(1, min(32, int(cpu_count() / 4))),
        help="Amount of threads used",
    )
    parser.add_argument("--debug", action="store_true", help="Debugging logging info")
    args = parser.parse_args()
    setup_logging(args.debug)
    return args


def prep_scurve_table(workload_or_trace, panthera_rollup_csv, rtl_rollup_csv):
    columns = []
    if workload_or_trace == "trace":
        columns = ["trace", "sys.soc_0.core_0.ipc"]
    if workload_or_trace == "workload":
        columns = ["workload", "sys.soc_0.core_0.ipc"]

    def use_column(c):
        return c in columns or c in TEMPORARY_METRICS

    def read_csv(path):
        df = pd.read_csv(path, usecols=use_column)
        if not set(columns).issubset(set(df.columns)):
            raise ValueError(f"Not all columns: {columns} are available")

        return df

    panthera_df = read_csv(panthera_rollup_csv)
    rtl_df = read_csv(rtl_rollup_csv)

    # since we get failuers in RTL and Panthera, here I am creaing a list of the common successful ones
    panthera_list = panthera_df[workload_or_trace].tolist()
    rtl_list = rtl_df[workload_or_trace].tolist()
    common_traces_workloads_list = list(set(rtl_list).intersection(panthera_list))
    logging.info(
        "RTL list count: "
        + str(len(rtl_list))
        + "\n"
        + "Panthera list count: "
        + str(len(panthera_list))
        + "\n"
        + "Common list count: "
        + str(len(common_traces_workloads_list))
    )

    new_rtl_list_df = (
        (rtl_df.loc[rtl_df[workload_or_trace].isin(common_traces_workloads_list)])
        .sort_values(workload_or_trace)
        .reset_index()
    )
    new_panthera_df = (
        (panthera_df.loc[panthera_df[workload_or_trace].isin(common_traces_workloads_list)])
        .sort_values(workload_or_trace)
        .reset_index()
    )

    # Build The new df where IPC and stats will be added
    dfs = {
        "Panthera": new_panthera_df,
        "RTL": new_rtl_list_df,
    }
    rtl_panthera_df = pd.DataFrame()
    rtl_panthera_df[workload_or_trace] = new_rtl_list_df[workload_or_trace]
    rtl_panthera_df["RTL_IPC/Panthera_IPC"] = (
        new_rtl_list_df["sys.soc_0.core_0.ipc"] / new_panthera_df["sys.soc_0.core_0.ipc"]
    )

    for name, metric in {"IPC": "sys.soc_0.core_0.ipc"}.items():
        for df_name, df in dfs.items():
            rtl_panthera_df[f"{df_name}_{name}"] = df[metric]

    for metric in TEMPORARY_METRICS:
        for df in dfs.values():
            if metric in df:
                rtl_panthera_df[metric] = df[metric]
                break

    return rtl_panthera_df, common_traces_workloads_list


def calculate_stats(output_scurve, rtl_panthera_df):
    rtl_panthera_df["stat"] = pd.Series(["GeoMean", "Within10%", "|GM|"])
    geomean = statistics.geometric_mean(rtl_panthera_df["RTL_IPC/Panthera_IPC"])

    temp_list = list()
    within_10_perc_count = 0
    for val in rtl_panthera_df["RTL_IPC/Panthera_IPC"]:
        if val < 1:
            temp_list.append(1 / val)
        else:
            temp_list.append(val)

        if val >= 0.9 and val <= 1.10:
            within_10_perc_count += 1

    absloute_geomean = statistics.geometric_mean(temp_list)
    within_10_perc = within_10_perc_count / len(rtl_panthera_df["RTL_IPC/Panthera_IPC"])

    rtl_panthera_df["stat values"] = pd.Series([str(geomean), str(within_10_perc), str(absloute_geomean)])
    rtl_panthera_df.drop(columns=list(TEMPORARY_METRICS), errors="ignore").to_csv(output_scurve, index=False)

    return rtl_panthera_df


def add_speculation_stats(rtl_panthera_df, workload_or_trace, path_to_regr, temp_output):
    traces_list = rtl_panthera_df[workload_or_trace]
    speculation_files_path = os.path.join(temp_output, "speculation_stats")
    if not os.path.exists(speculation_files_path):
        os.mkdir(speculation_files_path)

    for trace in traces_list:
        # get the event stream file
        trace_path = os.path.join(path_to_regr, trace) + ".es.xz"
        speculative_file_name = os.path.join(speculation_files_path, trace) + "_speculation_prefetching.txt"
        rtl_speculative_stat_cmd = (
            os.path.join(install_dir, "bin/evp")
            + " --es "
            + trace_path
            + " +python  "
            + os.path.join(install_dir, "config/events.processor/plugins/specmem.py")
        )

        # This is to avoid regenerating the speculative data, in case this data was created eariler for this run
        if not (os.path.isfile(speculative_file_name)):
            with open(speculative_file_name, "w") as es_spec_path:
                subprocess.run(rtl_speculative_stat_cmd, shell=True, stdout=es_spec_path)

        with open(speculative_file_name, "r") as es_spec_path:
            lines = es_spec_path.readlines()
            if not lines:
                logging.warning("Empty File: %s", speculative_file_name)

            value = 0
            for row in lines:
                if str(row).rstrip().startswith("|speculative_execution_prefetched"):
                    all_strings = str(row).split("|")
                    value = float(all_strings[2])
                    break

            row_num = rtl_panthera_df[rtl_panthera_df["trace"] == trace].index.item()
            rtl_panthera_df.at[row_num, "speculative #"] = value

    return rtl_panthera_df


def match_command(command, mismatch_file_name):
    with open(mismatch_file_name, "w") as es_same_path:
        subprocess.run(command, shell=True, stdout=es_same_path)


def add_mismatch_info(
    rtl_panthera_df,
    path_to_regr,
    path_to_panthera_es,
    es1_es_same_inum_start,
    es2_es_same_inum_start,
    max_instructions,
    temp_output,
    max_threads=1,
):
    traces_list = rtl_panthera_df["trace"]

    mismatch_files_path = os.path.join(temp_output, "mismatch_files")
    if not os.path.exists(mismatch_files_path):
        os.mkdir(mismatch_files_path)

    thread_args_list = []
    with ProcessPoolExecutor(max_workers=max_threads) as executor:
        for trace in traces_list:
            # get the event stream file
            rtl_trace_path = os.path.join(path_to_regr, trace) + ".es.xz"
            panthera_trace_path = os.path.join(path_to_panthera_es, trace) + ".es.xz"
            mismatch_file_name = os.path.join(mismatch_files_path, trace + ".same_code")
            trace_data = rtl_panthera_df.loc[rtl_panthera_df["trace"] == trace].iloc[0]
            instructions = trace_data.get("measurement_instructions", max_instructions)
            trace_max_instructions = min(max_instructions, instructions)

            def adjust_warmup(value):
                if (instructions - value) > 1:
                    return False, value
                return True, trace_data.get("warmup_instructions", value)

            es1_warmup_adjusted, trace_es1_warmup = adjust_warmup(es1_es_same_inum_start)
            es2_warmup_adjusted, trace_es2_warmup = adjust_warmup(es2_es_same_inum_start)
            if es1_warmup_adjusted or es2_warmup_adjusted:
                # Need to adjust max_instructions as it was set assuming the original warmup
                if trace_max_instructions == max_instructions:
                    trace_max_instructions += max(es1_es_same_inum_start, es2_es_same_inum_start)
                trace_max_instructions = trace_max_instructions - max(trace_es1_warmup, trace_es2_warmup)

            same_instr_stream_cmd = " ".join(
                [
                    os.path.join(install_dir, "bin/panthera"),
                    "snippets.es_same_path",
                    rtl_trace_path,
                    panthera_trace_path,
                    "--es1-inum-start",
                    str(trace_es1_warmup),
                    "--es2-inum-start",
                    str(trace_es2_warmup),
                    "--max-instructions",
                    str(trace_max_instructions),
                ]
            )

            logging.debug(same_instr_stream_cmd)
            thread_args_list.append(executor.submit(match_command, same_instr_stream_cmd, mismatch_file_name))

        for future in as_completed(thread_args_list):
            try:
                future.result()  # Get the result from each future
            except Exception as e:
                logging.exception("Exception occurred: %s", e)

    logging.info("Threads Done!")

    for trace in traces_list:
        mismatch_file_name = os.path.join(mismatch_files_path, trace + ".same_code")
        with open(mismatch_file_name, "r") as file:
            file_content = file.read()
            result = "Mismatch"
            if "Successfully" in file_content:
                result = "Matched"

        rtl_panthera_df.loc[rtl_panthera_df["trace"] == trace, "is_same_code?"] = result

    return rtl_panthera_df


def select_traces_to_debug(
    name,
    rtl_panthera_df,
    workload_or_trace,
    output_folder,
    lower_threshold,
    upper_threshold,
):
    selected_rows = []

    for _index, row in rtl_panthera_df.iterrows():
        cell_value = row["RTL_IPC/Panthera_IPC"]

        if cell_value > upper_threshold:  # RTL is faster
            selected_rows.append(row.iloc[0])

        if cell_value < lower_threshold:  # Panthera Faster
            selected_rows.append(row.iloc[0])

    selected_rows_df = pd.DataFrame(selected_rows, columns=[rtl_panthera_df.columns[0]])
    selected_rows_df.to_csv(os.path.join(output_folder, name), index=False, header=False)


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


def main():
    args = parse_args()
    ignore = load_ignore(args.ignore) if args.ignore else {}

    # check for provided arguments if they are enough
    if args.RTL_speculation_check and args.RTL_regr == "":
        logging.error("ERROR: RTL regr path is required to generate speculation Data")
        quit()
    if args.mismatch_check and (args.RTL_regr == "" or args.panthera_es_folder == ""):
        logging.error("ERROR: Please provide both -RTL and Panthera- ES folders to compare instr. streams")
        quit()

    if not os.path.exists(args.temp_output_folder):
        os.makedirs(args.temp_output_folder)

    logging.info("Generate S-Curve numbers")
    rtl_panthera_df, _common_rtl_panthera_list = prep_scurve_table(
        "trace", args.Panthera_rollup_csv, args.RTL_rollup_csv
    )

    # This will extract the number of speculation have been run on RTL.
    if args.RTL_speculation_check:
        logging.info("Exracting Speculation numbers")
        rtl_panthera_df = add_speculation_stats(rtl_panthera_df, "trace", args.RTL_regr, args.temp_output_folder)

    # check for each RTL and Panthera run if the same code were executed
    if args.mismatch_check:
        logging.info("Checking Same Code executed On RTL vs Panthera")
        rtl_panthera_df = add_mismatch_info(
            rtl_panthera_df,
            args.RTL_regr,
            args.panthera_es_folder,
            args.es1_es_same_inum_start,
            args.es2_es_same_inum_start,
            args.max_instructions,
            args.temp_output_folder,
            args.parallel,
        )

    if ignore:
        rtl_panthera_df = rtl_panthera_df[
            rtl_panthera_df.apply(
                lambda r: r["trace"] not in ignore
                or not isclose(r["RTL_IPC/Panthera_IPC"], ignore[r["trace"]], rel_tol=0.05),
                axis=1,
            )
        ]

    logging.info("Calculate Stats")
    rtl_panthera_df = calculate_stats(os.path.join(args.temp_output_folder, args.output_SCurve_csv), rtl_panthera_df)

    select_traces_to_debug(
        args.traces_to_debug,
        rtl_panthera_df,
        "trace",
        args.temp_output_folder,
        args.lower_threshold,
        args.upper_threshold,
    )


if __name__ == "__main__":
    main()
