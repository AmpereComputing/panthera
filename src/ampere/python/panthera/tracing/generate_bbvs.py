#!/usr/bin/env python3
# Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Run QEMU with the BBV collection tracer plugin to generate basic block vectors
for the given benchmark.

Any arguments supplied after '--' on the command-line are interpreted to be the
command-line to run/trace after booting the Linux kernel inside QEMU.
Additionally, any arguments supplied before the '--' which are not understood
by this program (i.e. included in the `--help` output) will be passed through
as arguments to QEMU.
"""

import argparse
import json
import os
import sys

from .. import install_dir
from . import arg_list_to_dict
from .run import run as qemu_run


def generate_bbvs(qemu_args, program_args, args):
    program = None
    if len(program_args) > 0:
        program = program_args.pop(0)
    if args.trace_process_name:
        profile_comm = args.trace_process_name
    else:
        profile_comm = os.path.basename(program)

    output_prefix = f"{args.output_prefix}.generate_bbvs"

    plugin_path = os.path.join(install_dir, "lib")
    plugin_lib = "bbv_generator"
    bbv_filename = f"{output_prefix}.bbv"
    mav_filename = f"{output_prefix}.mav"
    generation_log = f"{output_prefix}.generation_log.json"

    qemu_args = arg_list_to_dict(qemu_args)
    qemu_args["plugin"] = f"{plugin_path}/lib{plugin_lib}.so,profile-comm={profile_comm},"
    qemu_args["plugin"] += f"bbv-filename={bbv_filename},generation-log={generation_log},mav-filename={mav_filename},"
    qemu_args["plugin"] += (
        f"bbv-interval={args.bbv_interval},checkpoint-interval={args.checkpoint_interval},mav-interval={args.mav_interval},"
    )
    qemu_args["plugin"] += f"heartbeat-interval=100000000,profile-children={str(args.trace_children).lower()},"
    qemu_args["plugin"] += f"profile-el1={str(args.profile_el1).lower()},"
    qemu_args["plugin"] += (
        f"start-tracing-after-hint={str(args.use_hint_instructions).lower()},stop-tracing-after-hint={str(args.use_hint_instructions).lower()},"
    )
    qemu_args["plugin"] += f"debug={str(args.debug).lower()},"
    qemu_args["plugin"] += f"trace-mav={str(args.trace_mav).lower()},"
    qemu_args["plugin"] += f"mav-blocksize={args.mav_blocksize},"

    # kernel.panic=-1 causes the kernel to reboot
    qemu_args["append"] = [f"root={args.rootfs_device}", "perf.semihosting", "sysctl.kernel.panic=-1"]
    if program is not None:
        qemu_args["init"] = program
    if len(program_args) > 0:
        qemu_args["init_args"] = program_args
    qemu_args["hda"] = args.rootfs
    qemu_args["kernel"] = args.kernel
    qemu_args["dry_run"] = args.dry_run
    qemu_args["no-reboot"] = True  # Exit QEMU if the OS attempts to reboot
    qemu_args["d"] = "plugin"
    qemu_args["D"] = f"{output_prefix}.plugin.log"

    if not os.path.exists(f"{output_prefix}.json"):
        print(
            f"Writing results to {output_prefix}.json. This file can be used as "
            "the input to the simpoint tracing command, `panthera tracing.calculate_simpoints`"
        )
        with open(f"{output_prefix}.json", "w") as json_log:
            json_log.write(
                json.dumps(
                    {
                        "qemu_args": qemu_args,
                        "bbv_args": dict(vars(args)),
                        "program": program,
                        "program_args": program_args,
                        "profile_comm": profile_comm,
                        "bbv_filename": bbv_filename,
                        "mav_filename": mav_filename,
                        "bbv_generation_json": generation_log,
                    },
                    sort_keys=True,
                    indent=4,
                )
            )
    elif not args.dry_run:
        print(f"Error: Output .json file ({output_prefix}.json) already exists. Aborting")
        sys.exit(1)

    qemu_run(**qemu_args)


def parse_args(args):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output-prefix", help="Prefix for output filenames (extensions/suffixes will be added to this)", required=True
    )
    parser.add_argument(
        "--rootfs",
        default="rootfs.qcow2",
        help="QCOW2 rootfs image containing benchmark to trace (default: %(default)s)",
    )
    parser.add_argument(
        "--rootfs-device",
        default="/dev/vda rw",
        help='Device name to supply to "root=" on kernel command-line (default: %(default)s)',
    )
    parser.add_argument("--kernel", default="Image", help="Path to kernel image to boot (default: %(default)s)")
    parser.add_argument(
        "--trace-process-name", help="Trace this process name instead of the first string after --", type=str
    )
    parser.add_argument(
        "--bbv-interval", default=10000000, help="BBV interval length (default: %(default)s instructions)", type=int
    )
    parser.add_argument(
        "--checkpoint-interval",
        default=10000000000,
        help="Checkpoint interval length (default: %(default)s instructions)",
        type=int,
    )
    parser.add_argument(
        "--trace-children",
        default=False,
        action="store_true",
        help="Trace all children of the first identified process, too",
    )
    parser.add_argument(
        "--profile-el1",
        default=False,
        action="store_true",
        help="Trace EL1 and EL0 code in processes of interest (default is EL0 only)",
    )
    parser.add_argument(
        "--use-hint-instructions",
        default=False,
        action="store_true",
        help="Only trace between a pair of start and stop tracing hint instructions",
    )
    parser.add_argument(
        "--dry-run", default=False, action="store_true", help="Don't run, only display commands that would be executed"
    )
    parser.add_argument(
        "--debug", default=False, action="store_true", help="Run QEMU BBV generation plugin in debug mode"
    )
    parser.add_argument("--trace-mav", default=False, action="store_true", help="Trace Page Access Vectors (MAV)")
    parser.add_argument(
        "--mav-interval", nargs="?", help="BBV interval length (default: equal to bbv-interval)", type=int
    )
    parser.add_argument(
        "--mav-blocksize", default=4096, help="Size of memory block to track. Expects power of 2", type=int
    )
    return parser.parse_known_args(args)


class IncompatibleArgumentError(ValueError):
    pass


def check_args(qemu_args, program_args, args):
    if not args.trace_process_name:
        if len(program_args) == 0:
            raise IncompatibleArgumentError(
                "If the path to the binary to run is not supplied, --trace-process-name must be given explicitly since it cannot be inferred"
            )
    if args.mav_interval is None:
        args.mav_interval = args.bbv_interval


if __name__ == "__main__":
    args = sys.argv[1:]
    if "--" in args:
        pos = args.index("--")
        nonprogram_args = args[:pos]
        program_args = args[pos + 1 :]
    else:
        nonprogram_args = args
        program_args = []

    options, qemu_args = parse_args(nonprogram_args)
    check_args(qemu_args, program_args, options)
    generate_bbvs(qemu_args, program_args, options)
