#!/usr/bin/env python3
# Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Run QEMU, tracing intervals identified in a previous call to
`tracing.calculate_simpoints`.
"""

import argparse
import copy
import glob
import hashlib
import json
import os
import subprocess
import sys

from .. import install_dir
from .bbv import BBV
from .run import run as qemu_run


def sha256sum(filename):
    hasher = hashlib.sha256()
    with open(filename, "rb") as hash_file:
        block = hash_file.read(4096)
        while len(block) > 0:
            hasher.update(block)
            block = hash_file.read(4096)
    return hasher.hexdigest()


def find_best_checkpoint(checkpoint_array, instr_count, extra_insns, allowed_lateness=0):
    """
    Find a checkpoint that is not more than `allowed_lateness` after
    instr_count, attempting to find a checkpoint with `extra_insns` between the
    checkpoint and instr_count if possible. If multiple checkpoints satisfy
    both of the above conditions, prefer the checkpoint the fewest instructions
    away from `instr_count`.
    """
    best_checkpoint = None
    best_checkpoint_extra = 0
    best_checkpoint_count = 0

    for checkpoint in checkpoint_array:
        instrs = int(checkpoint["instr_count"])
        if instrs <= instr_count - extra_insns + allowed_lateness:
            if best_checkpoint is None or instrs > best_checkpoint_count:
                best_checkpoint = checkpoint["tag"]
                best_checkpoint_count = instrs
                best_checkpoint_extra = min(instr_count - instrs, extra_insns)
        elif instrs <= instr_count + allowed_lateness:
            checkpoint_extra = min(instr_count - instrs, extra_insns)
            if (
                best_checkpoint is None
                or checkpoint_extra > best_checkpoint_extra
                or (checkpoint_extra == best_checkpoint_extra and instrs > best_checkpoint_count)
            ):
                best_checkpoint = checkpoint["tag"]
                best_checkpoint_count = instrs
                best_checkpoint_extra = checkpoint_extra

    wait_instrs = max(instr_count - best_checkpoint_count, 0)

    return best_checkpoint, wait_instrs


def capture_traces(args, simpoint_json, bbv_gen_json, bbv_gen_json_path, interval, trace_filename_base):
    checkpoints = bbv_gen_json["checkpoints"]
    interval_size = int(bbv_gen_json["settings"]["bbv_interval"])
    offline_warmup_instrs = args.start_online_after_offline_instructions
    warmup_instrs = args.start_measured_after_online_instructions
    # RTL PerfVal snippets do not do online warmup
    if args.perfval_snippets:
        warmup_instrs = 0

    interval_instructions = interval_size * interval
    possible_warmup_instructions = min(interval_instructions, offline_warmup_instrs + warmup_instrs)
    best_checkpoint, wait_instrs = find_best_checkpoint(
        checkpoints, interval_instructions, possible_warmup_instructions
    )
    if not best_checkpoint and possible_warmup_instructions == 0:
        # Allow at most 0.05% of an interval or 5000 instructions of lateness
        # (whichever is larger). This is only allowed for the first few
        # intervals in a run, since we can search backwards to find a suitable
        # checkpoint without skid for all other intervals
        max_lateness = max(0.0005 * interval_size, 500)
        best_checkpoint, wait_instrs = find_best_checkpoint(
            checkpoints, interval_instructions, possible_warmup_instructions, allowed_lateness=max_lateness
        )
    if not best_checkpoint:
        raise RuntimeError(f"Unable to find suitable checkpoint for interval {interval}")

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

    plugin_path = os.path.join(install_dir, "lib")
    plugin_lib = "instruction_tracer"

    qemu_args = copy.deepcopy(simpoint_json["qemu_args"])
    # Replace BBV-generation plugin section with the instruction tracer
    plugin_args = f"{plugin_path}/lib{plugin_lib}.so,generation-log={bbv_gen_json_path},"
    plugin_args += f"checkpoint-tag={best_checkpoint},"
    plugin_args += f"start-tracing-after-instructions={wait_instrs},"
    plugin_args += f"start-measuring-after-online-warmup-instructions={warmup_instrs},"
    plugin_args += f"dcache-filter-size={args.dcache_filter_size_bytes},"
    plugin_args += f"icache-filter-size={args.icache_filter_size_bytes},"
    plugin_args += f"trace-filename-base={trace_filename_base},"
    plugin_args += f"debug={str(args.debug).lower()}"

    if offline_warmup_instrs > 0:
        plugin_args += f",start-online-warmup-after-offline-warmup-instructions={offline_warmup_instrs}"

    if args.trace_runlength > 0:
        plugin_args += (
            f",override-simpoint-stop-trigger=true,stop-tracing-after-measuring-instructions={args.trace_runlength}"
        )

    if args.perfval_snippets:
        number_snippets = max(int(interval_size / args.trace_runlength), 1)
        plugin_args += f",max-traces={number_snippets},trace-elf=true"

    qemu_args["plugin"] = plugin_args
    qemu_args["loadvm"] = best_checkpoint
    qemu_args["D"] = f"{trace_filename_base}.plugin.log"
    qemu_args["dry_run"] = args.dry_run
    qemu_args["singlestep"] = True

    # record arguments/settings used for posterity
    trace_interval_json = copy.deepcopy(simpoint_json)
    trace_interval_json["trace_interval_args"] = dict(vars(args))
    # An open file isn't going to serialize well to JSON, so replace it with its name
    trace_interval_json["trace_interval_args"]["input_json"] = trace_interval_json["trace_interval_args"][
        "input_json"
    ].name
    trace_interval_json["interval"] = interval
    trace_interval_json["interval_size"] = interval_size
    trace_interval_json["trace_filename_base"] = trace_filename_base
    trace_interval_json["trace_interval_qemu_args"] = qemu_args
    trace_interval_json["best_checkpoint"] = best_checkpoint
    trace_interval_json["wait_instrs"] = wait_instrs
    trace_interval_json["warmup_instrs"] = warmup_instrs
    trace_interval_json["offline_warmup_instrs"] = offline_warmup_instrs
    trace_interval_json["dcache_filter_size_bytes"] = args.dcache_filter_size_bytes
    trace_interval_json["icache_filter_size_bytes"] = args.icache_filter_size_bytes
    trace_interval_json["trace_runlength"] = args.trace_runlength

    if not args.dry_run:
        with open(f"{trace_filename_base}.json", "w") as json_log:
            json_log.write(json.dumps(trace_interval_json, sort_keys=True, indent=4))

    qemu_run(**qemu_args)

    return (offline_warmup_instrs, warmup_instrs)


def bbv_distances(trace_filename, bbv_gen_json, new_bbv_filename, interval):
    is2bbv = os.path.join(install_dir, "bin", "is2bbv")
    subprocess.run([is2bbv, trace_filename, new_bbv_filename])

    bbv = BBV()
    with open(new_bbv_filename, "r") as bbv_file:
        bbv.from_bbv_string(bbv_file.readline())

    bbv_filename = bbv_gen_json["settings"]["bbv_filename"]
    if not os.path.isabs(bbv_filename):
        bbv_filename = os.path.join(bbv_gen_json["settings"]["working_directory"], bbv_filename)
    orig_bbv = BBV()
    with open(bbv_filename, "r") as bbv_file:
        for i in range(interval):
            bbv_file.readline()
        orig_bbv.from_bbv_string(bbv_file.readline())

    return bbv.euclidean_distance(orig_bbv), bbv.manhattan_distance(orig_bbv)


def capture_intervals(args, simpoint_json):
    simpoints = simpoint_json["simpoints"]
    bbv_gen_json_path = simpoint_json["bbv_generation_json"]
    with open(bbv_gen_json_path) as bbv_gen_json_file:
        bbv_gen_json = json.load(bbv_gen_json_file)
    output_prefix = f"{simpoint_json['bbv_args']['output_prefix']}.trace_intervals"

    all_collections_succeeded = True
    traces = []
    total_weight = 0.0
    weighted_euclidean = 0.0
    weighted_manhattan = 0.0

    for interval, weight in simpoints.items():
        interval = int(interval)
        trace_filename_base = f"{simpoint_json['bbv_args']['output_prefix']}.{interval}"
        offline_warmup_instrs, warmup_instrs = capture_traces(
            args, simpoint_json, bbv_gen_json, bbv_gen_json_path, interval, trace_filename_base
        )

        if args.dry_run:
            continue

        is_filenames = glob.glob(f"{trace_filename_base}_[0-9]*.is.xz")
        if not args.perfval_snippets and len(is_filenames) != 1:
            print(f"Error: Expected 1 trace to be collected for {trace_filename_base}, found {len(is_filenames)}")
            all_collections_succeeded = False
        elif args.perfval_snippets and len(is_filenames) < 1:
            print(f"Warning: Did not collect any perf snippets for simpoint {trace_filename_base}")
            all_collections_succeeded = False

        interval_traces = []
        for trace_filename in is_filenames:
            basename = os.path.splitext(os.path.splitext(trace_filename)[0])[0]
            memws_filename = f"{basename}.ms.xz"
            bs_filename = f"{basename}.bs.xz"
            elf_filename = f"{basename}.elf"

            entry = {
                "id": str(basename),
                "file": os.path.basename(trace_filename),
                "offline_warmup_instrs": offline_warmup_instrs,
                "warmup_instrs": warmup_instrs,
                "sha256sum": sha256sum(trace_filename),
            }

            if os.path.exists(elf_filename):
                entry.update(
                    {"elf_file": os.path.basename(elf_filename), "elf_file_sha256sum": sha256sum(elf_filename)}
                )
            elif args.perfval_snippets:
                print(
                    f"Warning: Trace {trace_filename} is missing {elf_filename} (likely because it made an IO memory access) and will not be included in the JSON output of this command"
                )
                continue

            new_bbv_filename = f"{basename}.is.bbv"
            euclidean_distance, manhattan_distance = bbv_distances(
                trace_filename, bbv_gen_json, new_bbv_filename, interval
            )

            # TODO validate distances somehow - here or in a later command?
            entry.update({"euclidean_distance": euclidean_distance, "manhattan_distance": manhattan_distance})
            if offline_warmup_instrs > 0:
                entry.update(
                    {
                        "branch_warmup_file": os.path.basename(bs_filename),
                        "pan_branch_warmup_file": os.path.basename(bs_filename),
                        "full_offline_warmup_file": os.path.basename(memws_filename),
                        "pan_cache_preload_file": os.path.basename(memws_filename),
                        "branch_warmup_file_sha256sum": sha256sum(bs_filename),
                        "pan_cache_preload_file_sha256sum": sha256sum(memws_filename),
                    }
                )
            interval_traces.append(entry)

        # Now that we know how many traces 'succeeded', calculate individual
        # weights (divide by number of traces which succeeded), and append
        # trace to list of collected traces
        for trace in interval_traces:
            trace_weight = weight / float(len(interval_traces))
            total_weight += trace_weight
            weighted_euclidean += trace_weight * float(trace["euclidean_distance"])
            weighted_manhattan += trace_weight * float(trace["manhattan_distance"])

            trace.update({"weight": trace_weight})

            traces.append(trace)

        if len(interval_traces) == 0:
            print(
                f"Error: None of the {len(is_filenames)} snippets collected for {trace_filename_base} contains an ELF file. None of these snippets will be included in the JSON output of this command."
            )
            all_collections_succeeded = False

    # record identified traces and arguments/settings used for posterity
    trace_interval_json = simpoint_json
    trace_interval_json["trace_interval_args"] = dict(vars(args))
    # An open file isn't going to serialize well to JSON, so replace it with its name
    trace_interval_json["trace_interval_args"]["input_json"] = trace_interval_json["trace_interval_args"][
        "input_json"
    ].name
    trace_interval_json["traces"] = traces
    trace_interval_json["total_trace_weight"] = total_weight
    trace_interval_json["weighted_summed_euclidean_distance"] = weighted_euclidean
    trace_interval_json["weighted_summed_manhattan_distance"] = weighted_manhattan

    if not args.dry_run:
        with open(f"{output_prefix}.json", "w") as json_log:
            json_log.write(json.dumps(trace_interval_json, sort_keys=True, indent=4))

    return all_collections_succeeded


def nonnegative_int(value):
    value = int(value)
    if value < 0:
        raise argparse.ArgumentTypeError("%s must be a non-negative integer" % value)
    return value


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input-json",
        help="Path to <output_prefix>.calculate_simpoints.json file from tracing.calculate_simpoints run",
        type=open,
        required=True,
    )
    parser.add_argument(
        "--start-measuring-after-online-warmup-instructions",
        dest="start_measured_after_online_instructions",
        help="The number of instructions to collect before the beginning of the specified interval for uArch warmup. No online warmups is collected when `--perfval-snippets` is specified. (default: %(default)s)",
        default=1000000,
        type=nonnegative_int,
    )
    parser.add_argument(
        "--start-online-warmup-after-offline-warmup-instructions",
        dest="start_online_after_offline_instructions",
        help="The number of instructions to collect for offline warmup, before the beginning of the specified interval (default: %(default)s)",
        default=10000000,
        type=nonnegative_int,
    )
    parser.add_argument(
        "--trace-runlength",
        help="Override the BBV interval size and collect this many instructions at the simpoint interval (disabled by default)",
        default=0,
        type=nonnegative_int,
    )
    parser.add_argument(
        "--dcache-filter-size",
        dest="dcache_filter_size_bytes",
        help="Size of cache in bytes through which to filter memory stream accesses. Only accesses which miss this cache will show up in the stream (default: %(default)s)",
        default=65536,
        type=nonnegative_int,
    )
    parser.add_argument(
        "--icache-filter-size",
        dest="icache_filter_size_bytes",
        help="Size of cache in bytes through which to filter memory stream accesses. Only accesses which miss this cache will show up in the stream (default: %(default)s)",
        default=32768,
        type=nonnegative_int,
    )
    parser.add_argument(
        "--perfval-snippets",
        default=False,
        action="store_true",
        help="Collect snippets for RTL performance validation instead of projection traces. This enables tracing to ELF files, and will collect as many snippets of `--trace-runlength` as fit inside each simpoint interval (instead of only the first)",
    )
    parser.add_argument(
        "--dry-run", default=False, action="store_true", help="Don't run, only display commands that would be executed"
    )
    parser.add_argument("--debug", default=False, action="store_true", help="Run QEMU tracing plugin in debug mode")
    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()
    simpoint_json = json.load(args.input_json)
    if not capture_intervals(args, simpoint_json):
        sys.exit(1)
