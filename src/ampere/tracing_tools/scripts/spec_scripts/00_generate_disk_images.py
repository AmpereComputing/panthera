#!/usr/bin/env python3
# Copyright (c) 2019 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import argparse
import json
import os
import platform
import stat
import subprocess
import sys
import urllib.request

ALPINE_TARBALL = "alpine-minirootfs-3.9.0-aarch64.tar.gz"


def run(cmd, shell=False):
    """Log and run a command and error if it returns a nonzero return code"""
    if not shell and isinstance(cmd, (str, bytes)):
        cmd = cmd.split()
        print("+ " + " ".join(cmd))
    else:
        print("+ " + str(cmd))
    proc = subprocess.run(cmd, shell=shell)
    proc.check_returncode()


def download_alpine_image():
    if not os.path.exists(ALPINE_TARBALL):
        urllib.request.urlretrieve(
            f"http://dl-cdn.alpinelinux.org/alpine/v3.9/releases/aarch64/{ALPINE_TARBALL}", ALPINE_TARBALL
        )
    if not os.path.exists(ALPINE_TARBALL + ".sha256"):
        urllib.request.urlretrieve(
            f"http://dl-cdn.alpinelinux.org/alpine/v3.9/releases/aarch64/{ALPINE_TARBALL}.sha256",
            ALPINE_TARBALL + ".sha256",
        )

    run(["sha256sum", "-c", ALPINE_TARBALL + ".sha256"])


def unpack_alpine_image(to_dir):
    os.makedirs(to_dir)
    run(f"tar --warning=no-unknown-keyword -C {to_dir} -xf {ALPINE_TARBALL}")


def dir_to_ext4_qcow(from_dir, to_qcow, rootfs_size="250G"):
    # Create empty qcow image
    run(f"qemu-img create -f qcow2 -o nocow=on {to_qcow}.orig {rootfs_size}")

    # Use nbd to mount the qcow image in loopback mode and create ext4 rootfs
    # from contents of from_dir
    run(f"modprobe nbd")
    run(f"qemu-nbd --connect /dev/nbd0 {to_qcow}.orig")
    run(f"mkfs.ext4 -d {from_dir} -L rootfs /dev/nbd0")
    run(f"qemu-nbd --disconnect /dev/nbd0")

    # Shrink/compress image, remove uncompressed version
    run(f"qemu-img convert -O qcow2 -o nocow=on -c {to_qcow}.orig {to_qcow}")
    os.remove(to_qcow + ".orig")


def main():
    parser = argparse.ArgumentParser(description="Generate JSON file representing SPEC workloads to be simpointed")
    parser.add_argument(
        "basedir", help="Base directory to look for SPEC directories containing binaries/data", type=str
    )
    parser.add_argument("json", help="File to write JSON results to", type=argparse.FileType("w"))
    args = parser.parse_args()

    if os.geteuid() != 0:
        print("You must run this script as root (or sudo)!")
        sys.exit(1)
    if platform.machine() != "aarch64":
        print(
            "You must run this script on an AArch64 machine, as it expects to be able to natively run the first 525.x264 command"
        )
        sys.exit(1)

    print("Downloading and checking Alpine Linux image...")
    download_alpine_image()

    cmdlines = []

    for benchmark_dir in [d.name for d in os.scandir(args.basedir) if d.is_dir()]:
        benchmark = benchmark_dir.replace("-", ".")
        full_benchmark_dir = os.path.join(args.basedir, benchmark_dir)
        cmdline_num = 0
        for line in open(os.path.join(full_benchmark_dir, "cmdline")):
            cmdline = line.strip()
            if len(cmdline) == 0:
                continue
            cmdline_num += 1

            rootfs_dir = f"{benchmark}-{cmdline_num}_rootfs"
            disk_image = f"{benchmark}-{cmdline_num}.qcow"
            init_script = f"/opt/{benchmark}/run.sh"

            cmdlines.append(
                {
                    "benchmark": benchmark,
                    "benchmark_dir": benchmark_dir,
                    "cmdline": cmdline,
                    "cmdline_num": str(cmdline_num),
                    "cmdline_name": str(cmdline_num),
                    "disk_image": disk_image,
                    "init_script": init_script,
                }
            )

            print(f"Creating disk image for {benchmark}-{cmdline_num}...")
            unpack_alpine_image(rootfs_dir)

            # Copy benchmark binary and data into rootfs directory
            run(f"cp -r {full_benchmark_dir} {rootfs_dir}/opt/{benchmark}")

            # If we're creating a rootfs for the second command-line of
            # 525.x264, run the first 525.x264 command first. These
            # command-lines are two stages of the same operation, so the second
            # depends upon the output from the first, unlike the remainder of
            # the SPEC2017 benchmarks
            if benchmark == "525.x264" and cmdline_num is 2:
                first_cmd = cmdlines[len(cmdlines) - 2]
                assert first_cmd["benchmark"] == "525.x264"
                assert first_cmd["cmdline_num"] == "1"
                assert platform.machine() == "aarch64"

                cwd = os.getcwd()
                os.chdir(f"{rootfs_dir}/opt/{benchmark}/")
                run(first_cmd["cmdline"], shell=True)
                os.chdir(cwd)

            # Write run script to be called via init= on kernel command-line
            # and ensure it is executable
            with open(f"{rootfs_dir}{init_script}", "w") as run_script:
                run_script.write("#!/bin/sh\n\n")
                run_script.write("mount -t proc none /proc\n")
                run_script.write(f"cd /opt/{benchmark}\n")
                run_script.write(f"{cmdline}\n")
            st = os.stat(f"{rootfs_dir}{init_script}")
            os.chmod(f"{rootfs_dir}{init_script}", st.st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

            # Create a qcow image containing an ext4 filesystem with the
            # contents of our rootfs directory
            dir_to_ext4_qcow(rootfs_dir, disk_image)

    json_cmdlines = json.dumps(cmdlines, indent=4)
    args.json.write(json_cmdlines)

    print(
        "Don't forget to `chown` all the *.qcow images this created to your user, or otherwise change the permissions so that your user can read/write to them"
    )
    print(
        f'JSON saved to {args.json.name} - you probably want to update the "cmdline_name" fields to something based on the inputs'
    )


if __name__ == "__main__":
    main()
