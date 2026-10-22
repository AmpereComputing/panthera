#!/usr/bin/env python3

#
# Copyright (c) 2018 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#

import argparse
import hashlib
import json
import os
import subprocess

from bbv import BBV
from standardize_bbvs import standardize


def sha256sum(filename):
    hasher = hashlib.sha256()
    with open(filename, "rb") as hash_file:
        block = hash_file.read(4096)
        while len(block) > 0:
            hasher.update(block)
            block = hash_file.read(4096)
    return hasher.hexdigest()


def standardize_bbvs(gen_json):
    """
    Standardize the BBV file output from the QEMU plugin and return the path to
    the standardized version
    """
    bbv_filename = gen_json["settings"]["bbv_filename"]
    if not os.path.isabs(bbv_filename):
        bbv_filename = os.path.join(gen_json["settings"]["working_directory"], bbv_filename)

    base, ext = os.path.splitext(bbv_filename)
    std_bbv_filename = base + ".std" + ext
    standardize(bbv_filename, std_bbv_filename)

    return std_bbv_filename


def run_simpoints(bbv_path, maxk, bic_threshold):
    """
    Run the simpoint tool for the given standardized BBV file and return a
    dictionary keyed by the selected intervals with their weights as values.
    """
    simpoint_path = os.path.splitext(bbv_path)[0] + ".simpoints"
    weights_path = os.path.splitext(bbv_path)[0] + ".weights"
    labels_path = os.path.splitext(bbv_path)[0] + ".labels"
    log = os.path.splitext(bbv_path)[0] + ".simpoints.log"

    # Run the simpoint tool
    with open(log, "w") as logfile:
        subprocess.run(
            [
                "simpoint",
                "-loadFVFile", bbv_path,
                "-maxK", str(maxk),
                "-bicThreshold", bic_threshold,
                "-saveSimpoints", simpoint_path,
                "-saveSimpointWeights", weights_path,
                "-saveLabels", labels_path,
            ],
            stderr=subprocess.STDOUT,
            stdout=logfile,
            check=True,
        )  # fmt: skip

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

    return simpoints


def find_best_checkpoint(checkpoint_array, instr_count, allowed_lateness=0):
    best_checkpoint = None
    best_checkpoint_count = 0

    for checkpoint in checkpoint_array:
        instrs = int(checkpoint["instr_count"])
        if instrs <= instr_count + allowed_lateness:
            if best_checkpoint == None or instrs > best_checkpoint_count:
                best_checkpoint = checkpoint["tag"]
                best_checkpoint_count = instrs

    wait_instrs = max(instr_count - best_checkpoint_count, 0)

    return best_checkpoint, wait_instrs


def capture_trace(
    gen_json,
    gen_json_path,
    interval,
    qemu_cmd,
    plugin,
    trace_filename_base,
    warmup_instrs,
    offline_warmup_instrs,
    runlength,
    dcache_filter_size_bytes,
    icache_filter_size_bytes,
):
    interval_size = int(gen_json["settings"]["bbv_interval"])

    best_checkpoint, wait_instrs = find_best_checkpoint(gen_json["checkpoints"], interval_size * interval)
    if not best_checkpoint and interval == 0:
        # Allow at most 0.05% of an interval or 5000 instructions of lateness
        # (whichever is larger). This is only allowed for the first interval in
        # a run, since we can search backwards to find a suitable checkpoint
        # without skid for all other intervals
        max_lateness = max(0.0005 * interval_size, 500)

        best_checkpoint, wait_instrs = find_best_checkpoint(
            gen_json["checkpoints"], interval_size * interval, allowed_lateness=max_lateness
        )
    if not best_checkpoint:
        print("Unable to find suitable checkpoint for interval %d" % (interval))

    # shift over trace start when we have online warmup
    if warmup_instrs >= wait_instrs:
        warmup_instrs = wait_instrs
        wait_instrs = 0
    else:
        wait_instrs -= warmup_instrs

    # further shift over trace start when we have offline warmup
    if offline_warmup_instrs >= wait_instrs:
        offline_warmup_instrs = wait_instrs
        wait_instrs = 0
    else:
        wait_instrs -= offline_warmup_instrs

    qemu_cmd += ' -loadvm "%s"' % (best_checkpoint)
    qemu_cmd += (
        ' -plugin %s,arg="--generation-log \\"%s\\" --checkpoint-tag \\"%s\\" --start-tracing-after-instructions %d --start-measuring-after-online-warmup-instructions %d --dcache-filter-size %d --icache-filter-size %d'
        % (
            plugin,
            gen_json_path,
            best_checkpoint,
            wait_instrs,
            warmup_instrs,
            dcache_filter_size_bytes,
            icache_filter_size_bytes,
        )
    )

    if offline_warmup_instrs > 0:
        qemu_cmd += " --start-online-warmup-after-offline-warmup-instructions %d" % (offline_warmup_instrs)

    if runlength > 0:
        qemu_cmd += " --override-simpoint-stop-trigger --stop-tracing-after-measuring-instructions %d" % (runlength)

    qemu_cmd += ' --trace-filename-base \\"%s\\""' % (trace_filename_base)
    qemu_cmd += f" -d plugin -D {trace_filename_base}_tracing_plugin.log"

    print("\nRunning: %s" % (qemu_cmd))
    subprocess.run(qemu_cmd, shell=True)

    return warmup_instrs


def bbv_distances(trace_filename, gen_json, new_bbv_filename, interval, is2bbv):
    subprocess.run([is2bbv, trace_filename, new_bbv_filename])

    bbv = BBV()
    with open(new_bbv_filename, "r") as bbv_file:
        bbv.from_bbv_string(bbv_file.readline())

    bbv_filename = gen_json["settings"]["bbv_filename"]
    if not os.path.isabs(bbv_filename):
        bbv_filename = os.path.join(gen_json["settings"]["working_directory"], bbv_filename)
    orig_bbv = BBV()
    with open(bbv_filename, "r") as bbv_file:
        for i in range(interval):
            bbv_file.readline()
        orig_bbv.from_bbv_string(bbv_file.readline())

    return bbv.euclidean_distance(orig_bbv), bbv.manhattan_distance(orig_bbv)


def nonnegative_int(value):
    value = int(value)
    if value < 0:
        raise argparse.ArgumentTypeError("%s must be a non-negative integer" % value)
    return value


def main():
    parser = argparse.ArgumentParser(
        description="Run the simpoint tool and capture instruction traces for identified intervals."
    )
    parser.add_argument(
        "--generation-log",
        dest="bbv_gen_log",
        help="The BBV generation JSON metadata generated by the QEMU plugin",
        type=open,
        required=True,
    )
    parser.add_argument(
        "--qemu-command",
        dest="qemu_cmd",
        help="The QEMU command-line with arguments. This will be amended with the appropriate commands to capture the instruction stream.",
        required=True,
    )
    parser.add_argument(
        "--plugin", dest="plugin", help="The QEMU instruction stream plugin .so file", type=open, required=True
    )
    parser.add_argument(
        "--trace-base",
        dest="trace_base",
        help='The base filename for the instruction traces. For instance, if you want the trace for the 5th interval to be saved to "/path/to/benchmark.5.is.xz", supply "/path/to/benchmark" as this parameter.',
        required=True,
    )
    parser.add_argument(
        "--start-measuring-after-online-warmup-instructions",
        dest="warmup_insns",
        help="The number of instructions to collect before the beginning of the specified interval for uArch warmup.",
        default=1000010,
        type=nonnegative_int,
    )
    parser.add_argument(
        "--start-online-warmup-after-offline-warmup-instructions",
        dest="offline_warmup_insns",
        help="The number of instructions to collect for offline warmup, before the beginning of the specified interval.",
        default=10000000,
        type=nonnegative_int,
    )
    parser.add_argument(
        "--trace-runlength",
        dest="trace_runlength",
        help="Override the BBV interval size and collect this many instructions at the simpoint interval.",
        default=0,
        type=nonnegative_int,
    )
    parser.add_argument(
        "--dcache-filter-size",
        dest="dcache_filter_size_bytes",
        help="Size of data cache in bytes through which to filter memory stream accesses. Only data accesses which miss this dcache will show up in the stream",
        default=65536,
        type=nonnegative_int,
    )
    parser.add_argument(
        "--icache-filter-size",
        dest="icache_filter_size_bytes",
        help="Size of code cache in bytes through which to filter memory stream accesses. Only code accesses which miss this icache will show up in the stream",
        default=32768,
        type=nonnegative_int,
    )
    parser.add_argument(
        "--simpoint-maxk",
        dest="maxk",
        help="Maximum number of clusters for simpoint tool to consider",
        default=20,
        type=nonnegative_int,
    )
    parser.add_argument(
        "--simpoint-bic-threshold",
        dest="bic_threshold",
        help="Multiplier used by simpoint tool, passed directly to simpoint's `-bicThreshold` option.",
        default="0.9",
        type=str,
    )

    args = parser.parse_args()
    json_log = json.load(args.bbv_gen_log)

    bbv_path = standardize_bbvs(json_log)
    simpoints = run_simpoints(bbv_path, args.maxk, args.bic_threshold)

    instruction_streams = []
    weighted_euclidean = 0.0
    weighted_manhattan = 0.0

    for interval, weight in simpoints.items():
        # TODO Do this in parallel
        trace_filename_base = "%s.%d" % (args.trace_base, interval)
        trace_filename = f"{trace_filename_base}_0.is.xz"
        memws_filename = f"{trace_filename_base}_0.ms.xz"
        warmup_instrs = capture_trace(
            json_log,
            args.bbv_gen_log.name,
            interval,
            args.qemu_cmd,
            args.plugin.name,
            trace_filename_base,
            args.warmup_insns,
            args.offline_warmup_insns,
            args.trace_runlength,
            args.cache_filter_size_bytes,
        )

        new_bbv_filename = f"{trace_filename_base}_0.is.bbv"
        euclidean_distance, manhattan_distance = bbv_distances(
            trace_filename,
            json_log,
            new_bbv_filename,
            interval,
            os.path.join(os.path.dirname(os.path.dirname(args.plugin.name)), "bin", "is2bbv"),
        )

        weighted_euclidean += weight * euclidean_distance
        weighted_manhattan += weight * manhattan_distance

        instruction_streams.append(
            {
                "id": str(interval),
                "weight": str(weight),
                "file": os.path.basename(trace_filename),
                "full_offline_warmup_file": os.path.basename(memws_filename),
                "pan_cache_preload_file": os.path.basename(memws_filename),
                "warmup_instrs": warmup_instrs,
                "sha256sum": sha256sum(trace_filename),
                "pan_cache_preload_file_sha256sum": sha256sum(memws_filename),
                "euclidean_distance": str(euclidean_distance),
                "manhattan_distance": str(manhattan_distance),
            }
        )

    manifest_filename = os.path.join(os.path.dirname(trace_filename), "manifest.json")
    profiled_pids = []
    if "profile_pids" in json_log["settings"]:
        profiled_pids = json_log["settings"]["profile_pids"]
    else:
        profiled_pids = [json_log["settings"]["profile_pid"]]
    with open(manifest_filename, "w") as manifest:
        json_info = {
            "workload": {
                "profiled_pids": profiled_pids,
            },
            "collection_methodology": {
                "type": "sampling",
                "sampling_methodology": "simpoints",
                "simpoint_interval_size": json_log["settings"]["bbv_interval"],
                "simpoint_total_instructions": json_log["settings"]["total_instructions"],
                "weighted_euclidean_distance": str(weighted_euclidean),
                "weighted_manhattan_distance": str(weighted_manhattan),
            },
            "instruction_streams": {"threading_model": "software", "threads": instruction_streams},
        }
        json.dump(json_info, manifest, indent=4)


if __name__ == "__main__":
    main()
