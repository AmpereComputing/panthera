#!/usr/bin/env python3
# Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Run QEMU with the simple tracer plugin to generate an instruction trace.

FIXME: Expand trace triggering options
"""

import argparse
import os
import sys

from .. import install_dir
from . import arg_list_to_dict
from .run import run as qemu_run


def run(
    qemu_args,
    program_args,
    output=None,
    start_offline_warmup_after_instructions=None,
    start_online_warmup_after_offline_warmup_instructions=None,
    start_measuring_after_online_warmup_instructions=None,
    start_tracing_after_pc=None,
    start_tracing_after_pc_occurrences=None,
    cooldown=1,
    trace_elf=False,
    rootfs_hda="rootfs.qcow2",
    max_instructions=0,
    dry_run=False,
    debug=False,
    ramdisk=None,
    trace_everything=False,
    skip_async=None,
    profile_children=True,
):
    program = program_args.pop(0)
    program_base = os.path.basename(program)
    if not program.startswith("/"):
        program = "/" + program

    if output is None:
        output = program_base

    plugin_path = os.path.join(install_dir, "lib")
    plugin_lib = "instruction_tracer"

    qemu_args = arg_list_to_dict(qemu_args)
    qemu_args["plugin"] = f"{plugin_path}/lib{plugin_lib}.so,profile-comm={program_base},"

    if profile_children:
        qemu_args["plugin"] += "profile-children=true,"

    if skip_async:
        qemu_args["plugin"] += "skip-async=true,"

    if trace_everything:
        qemu_args["plugin"] += f"start-tracing-after-instructions=1,"
    else:
        if start_offline_warmup_after_instructions is not None:
            qemu_args["plugin"] += f"start-tracing-after-instructions={start_offline_warmup_after_instructions},"
        elif start_tracing_after_pc is not None:
            qemu_args["plugin"] += f"start-tracing-after-pc={start_tracing_after_pc},"
            qemu_args["plugin"] += f"start-tracing-after-pc-occurrences={start_tracing_after_pc_occurrences},"
        else:
            qemu_args["plugin"] += "start-tracing-after-hint=true,"

        if start_online_warmup_after_offline_warmup_instructions is not None:
            qemu_args["plugin"] += (
                f"start-online-warmup-after-offline-warmup-instructions={start_online_warmup_after_offline_warmup_instructions},"
            )
        else:
            qemu_args["plugin"] += "start-online-warmup-after-hint=true,"

        if start_measuring_after_online_warmup_instructions is not None:
            qemu_args["plugin"] += (
                f"start-measuring-after-online-warmup-instructions={start_measuring_after_online_warmup_instructions},"
            )
        else:
            qemu_args["plugin"] += "start-measuring-after-hint=true,"

    qemu_args["plugin"] += f"cooldown-instructions={cooldown}"
    qemu_args["plugin"] += f",trace-filename-base={output}"
    if max_instructions > 0:
        qemu_args["plugin"] += f",stop-tracing-after-measuring-instructions={max_instructions}"
    elif not trace_everything:
        qemu_args["plugin"] += ",stop-tracing-after-hint=true"

    if trace_elf:
        qemu_args["plugin"] += ",trace-elf=true"

    qemu_args["d"] = "plugin"
    if debug:
        qemu_args["plugin"] += ",debug=true"

    if ramdisk is not None:
        qemu_args["append"] = []
        qemu_args["initrd"] = ramdisk
    else:
        qemu_args["append"] = ["root=/dev/vda"]
        qemu_args["hda"] = rootfs_hda

    qemu_args["append"] += ["perf.semihosting"]
    qemu_args["init"] = program
    qemu_args["init_args"] = program_args
    qemu_args["dry_run"] = dry_run
    qemu_args["singlestep"] = True

    qemu_run(**qemu_args)


def parse_args(args):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", help="Output trace name (without .is or .elf extension)")
    parser.add_argument(
        "--start-online-warmup-after-offline-warmup-instructions",
        default=1,
        help="Number of offline warmup instructions",
    )
    parser.add_argument(
        "--start-measuring-after-online-warmup-instructions",
        default=1,
        help="Number of warmup instructions",
    )
    parser.add_argument(
        "--start-tracing-after-pc",
        default=0,
        help="Start tracing after pc",
    )
    parser.add_argument(
        "--start-tracing-after-pc-occurrences",
        default=1,
        help="Start tracing after pc occurences",
    )
    # using -C to avoid argparse collision when passing through args for -cpu to QEMU
    parser.add_argument("--cooldown", default=10000, help="Number of cooldown instructions")
    parser.add_argument(
        "--trace-elf",
        default=False,
        action="store_true",
        help="Also trace ELF files alongside instruction streams",
    )
    parser.add_argument("--rootfs-hda", help="QCOW2 rootfs image containing binary to trace", type=str)
    parser.add_argument("--ramdisk", help="Ramdisk image containing binary to trace", type=str)
    parser.add_argument(
        "--trace_everything",
        action="store_true",
        help="Trace entire execution rather than hint-delimited regions",
    )
    parser.add_argument(
        "--max-instructions",
        default=0,
        type=int,
        help="Stop capturing after X instructions instead of on stop measurement hint",
    )
    parser.add_argument("--debug", default=False, action="store_true", help="Run plugin in debug mode")
    parser.add_argument(
        "--dry-run",
        default=False,
        action="store_true",
        help="Don't run, display commands that would be executed",
    )
    return parser.parse_known_args(args)


if __name__ == "__main__":
    args = sys.argv[1:]
    if "--" in args:
        pos = args.index("--")
        qemu_args = args[:pos]
        program_args = args[pos + 1 :]
    else:
        qemu_args = []
        program_args = args
    options, qemu_args = parse_args(qemu_args)
    run(qemu_args, program_args, **vars(options))
