#!/usr/bin/env python3
# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Leverage other tracing utility scripts for one-stop microbenchmark tracing
"""

import argparse
import os
import platform
import stat
import sys
import tempfile

from . import create_image, trace


def parse_args(args):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", help="Path to kernel image", required=True)
    parser.add_argument("--output", help="Output trace name (without .is or .elf extension)")
    parser.add_argument(
        "--start-offline-warmup-after-instructions",
        default=None,
        help="Number of process instructions to ignore prior to offline warmup (disables hint-based offline warmup start)",
    )
    parser.add_argument(
        "--start-online-warmup-after-offline-warmup-instructions",
        default=None,
        help="Number of offline warmup instructions to trace prior to online warmup (disables hint-based online warmup start)",
    )
    parser.add_argument(
        "--start-measuring-after-online-warmup-instructions",
        default=None,
        help="Number of online warmup instructions to trace prior to measured instructions (disables hint-based measured start)",
    )
    parser.add_argument(
        "--start-tracing-after-pc",
        default=None,
        help="Start tracing after pc",
    )
    parser.add_argument(
        "--start-tracing-after-pc-occurrences",
        default=None,
        help="Start tracing after pc occurences",
    )

    # using -C to avoid argparse collision when passing through args for -cpu to QEMU
    parser.add_argument("--cooldown", default=5000, help="Number of cooldown instructions")
    parser.add_argument(
        "--trace-elf",
        default=False,
        action="store_true",
        help="Also trace ELF files alongside instruction streams",
    )
    parser.add_argument(
        "--trace-everything",
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
    parser.add_argument("--environment", default=[], nargs="*")
    parser.add_argument("--execute", help="Binary file to load into a ramdisk and trace", required=True)
    return parser.parse_known_args(args)


def generate_init(micro_invocation, init_path="./init", environment=None):
    init_preamble = """#!/bin/sh

export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

[ -d /dev ] || mkdir -m 0755 /dev
[ -d /dev/shm ] || mkdir -m 0755 /dev/shm
[ -d /dev/pts ] || mkdir -m 0755 /dev/pts
[ -f /dev/null ] || mknod /dev/null c 1 3
[ -d /root ] || mkdir -m 0700 /root
[ -d /sys ] || mkdir /sys
[ -d /proc ] || mkdir /proc
[ -d /tmp ] || mkdir /tmp
mkdir -p /var/lock
mount -t sysfs -o nodev,noexec,nosuid sysfs /sys
mount -t proc -o nodev,noexec,nosuid proc /proc

mount -t devtmpfs -o nosuid,mode=0755 udev /dev
mkdir /dev/pts
mount -t devpts -o noexec,nosuid,gid=5,mode=0620 devpts /dev/pts || true

"""
    init_postamble = """

/sbin/poweroff -f

panic \"Init reached unreachable condition\"
"""

    with open(init_path, "w") as init_file:
        init_file.write(init_preamble)
        env_string = ""
        for var in environment:
            env_string += f"{var}={environment[var]} "
        init_file.write(
            env_string + "/" + os.path.basename(str(micro_invocation[0])) + " " + " ".join(micro_invocation[1:])
        )
        init_file.write(init_postamble)

    os.chmod(
        init_path,
        stat.S_IXUSR | stat.S_IRUSR | stat.S_IWUSR | stat.S_IXGRP | stat.S_IRGRP | stat.S_IXOTH | stat.S_IROTH,
    )


if __name__ == "__main__":
    args, other = parse_args(sys.argv[1:])

    micro_invocation = args.execute.split(" ")
    micro_binary_path = micro_invocation[0]

    temp_dir = tempfile.TemporaryDirectory()
    temp_init_path = os.path.join(temp_dir.name, "init")
    temp_ramdisk_path = os.path.join(temp_dir.name, "ramdisk.img")
    environment = dict([e.split("=") for e in args.environment])
    generate_init(micro_invocation, temp_init_path, environment=environment)

    if not args.dry_run:
        create_image.run(
            ramdisk=True,
            output=temp_ramdisk_path,
            files=[f"{micro_binary_path}", temp_init_path, "/"],
        )

    cpu_options = ["max"]
    cpu_options.append("pauth=on,pauth-qarma3=on")
    cpu_options.append("sve128=on,sve-max-vq=1")
    cpu_options.append("sme=off")
    cpu_options_str = ",".join(cpu_options)

    trace.run(
        qemu_args=["-kernel", args.kernel, "-cpu", cpu_options_str, "-m", "4G", "-M", "virt"] + other,
        program_args=micro_invocation,
        ramdisk=temp_ramdisk_path,
        output=args.output,
        start_offline_warmup_after_instructions=args.start_offline_warmup_after_instructions,
        start_online_warmup_after_offline_warmup_instructions=args.start_online_warmup_after_offline_warmup_instructions,
        start_measuring_after_online_warmup_instructions=args.start_measuring_after_online_warmup_instructions,
        start_tracing_after_pc=args.start_tracing_after_pc,
        start_tracing_after_pc_occurrences=args.start_tracing_after_pc_occurrences,
        cooldown=args.cooldown,
        trace_elf=args.trace_elf,
        trace_everything=args.trace_everything,
        max_instructions=args.max_instructions,
        debug=args.debug,
        dry_run=args.dry_run,
        skip_async=True,
    )
