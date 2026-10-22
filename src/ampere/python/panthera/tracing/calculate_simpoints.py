#!/usr/bin/env python3
# Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Run the simpoint tool on the output of the 'tracing.generate_bbvs' command to
determine which intervals can be used to represent the traced benchmark.

The output of this command can then be fed to 'tracing.trace_intervals' to
capture traces for those identified intervals.
"""

import argparse
import json
import shlex
import subprocess

from .standardize_bbvs import standardize


def calculate_simpoints(args, bbv_json):
    """
    Run the simpoint tool for the given standardized BBV file and output a
    dictionary keyed by the selected intervals with their weights as values to
    the JSON output file.
    """

    bbv_path = bbv_json["bbv_filename"]
    bbv_generation_json = bbv_json["bbv_generation_json"]

    output_prefix = f"{bbv_json['bbv_args']['output_prefix']}.calculate_simpoints"
    std_bbv_path = f"{output_prefix}.std.bbv"
    simpoint_path = f"{output_prefix}.simpoints"
    weights_path = f"{output_prefix}.weights"
    labels_path = f"{output_prefix}.labels"
    log_path = f"{output_prefix}.simpoints.log"

    # 'Standardize' the BBV file output from the QEMU plugin - convert it from
    # being indexed by program counter to being indexed by integers
    standardize(bbv_path, std_bbv_path)

    # Run the simpoint tool
    command = [
        "simpoint",
        "-loadFVFile",
        std_bbv_path,
        "-maxK",
        str(args.simpoint_maxk),
        "-bicThreshold",
        args.simpoint_bic_threshold,
        "-saveSimpoints",
        simpoint_path,
        "-saveSimpointWeights",
        weights_path,
        "-saveLabels",
        labels_path,
    ]
    command_str = " ".join(shlex.quote(arg) for arg in command)
    print(f"running:\n\t{command_str}")
    with open(log_path, "w") as logfile:
        subprocess.run(command, stderr=subprocess.STDOUT, stdout=logfile, check=True)

    # Pull the identified simpoints and weights from their files and associate
    # them with each other
    weights = {}
    simpoints = {}
    with open(weights_path, "r") as weights_file:
        for line in weights_file:
            weight, idx = line.split()
            weights[int(idx)] = float(weight)
    with open(simpoint_path, "r") as simpoints_file:
        for line in simpoints_file:
            interval, idx = [int(v) for v in line.split()]
            simpoints[interval] = weights[idx]

    # output options, as well as identified simpoints to JSON file
    simpoint_json = bbv_json
    simpoint_json["simpoint_args"] = dict(vars(args))
    simpoint_json["simpoints"] = simpoints
    simpoint_json["standardized_bbvs_path"] = std_bbv_path
    simpoint_json["simpoint_path"] = simpoint_path
    simpoint_json["simpoint_weights_path"] = weights_path
    simpoint_json["simpoint_labels_path"] = labels_path
    # An open file isn't going to serialize well to JSON, so replace it with its name
    simpoint_json["simpoint_args"]["input_json"] = simpoint_json["simpoint_args"]["input_json"].name

    print(
        f"Writing results to {output_prefix}.json. This file can be used as "
        "the input to the simpoint tracing command, `panthera tracing.trace_intervals`"
    )
    with open(f"{output_prefix}.json", "w") as json_log:
        json_log.write(json.dumps(simpoint_json, sort_keys=True, indent=4))


def nonnegative_int(value):
    value = int(value)
    if value < 0:
        raise argparse.ArgumentTypeError("%s must be a non-negative integer" % value)
    return value


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input-json",
        help="Path to <output_prefix>.generate_bbvs.json file from tracing.generate_bbvs run",
        type=open,
        required=True,
    )
    parser.add_argument(
        "--simpoint-maxk",
        help="Maximum number of clusters for simpoint tool to consider (default: %(default)s)",
        default=20,
        type=nonnegative_int,
    )
    parser.add_argument(
        "--simpoint-bic-threshold",
        help="Multiplier used by simpoint tool, passed directly to simpoint's `-bicThreshold` option (default: %(default)s)",
        default="0.9",
        type=str,
    )
    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()
    bbv_json = json.load(args.input_json)
    calculate_simpoints(args, bbv_json)
