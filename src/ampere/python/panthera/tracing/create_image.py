#!/usr/bin/env python3
# Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Create a QEMU disk image (qcow2 or ramdisk format).

This script must be run as root unless generating a ramdisk image,
so you might be prompted for your password for sudo.
"""

import argparse
import os
import subprocess
import sys
import tempfile

import distutils.dir_util
import distutils.file_util

DEFAULT_ROOTFS = "http://dl-cdn.alpinelinux.org/alpine/v3.8/releases/aarch64/alpine-minirootfs-3.8.1-aarch64.tar.gz"


class BuildDirectory:
    def __init__(self, dir=None):
        if dir is None:
            self.tmpdir = tempfile.TemporaryDirectory()
            self.dir = self.tmpdir.name
        else:
            os.makedirs(dir, exist_ok=True)
            self.dir = dir
        self.cwd = None

    def __enter__(self):
        self.cwd = os.getcwd()
        os.chdir(self.dir)

    def __exit__(self, type, value, tb):
        if tb is None and self.cwd:
            os.chdir(self.cwd)


def run(
    rootfs=DEFAULT_ROOTFS,
    rootfs_size="250G",
    ramdisk=False,
    output="rootfs.qcow2",
    build_dir=None,
    uid=-1,
    gid=-1,
    files=None,
):
    cwd = os.getcwd()
    with BuildDirectory(build_dir):
        os.mkdir("rootfs")
        unpack_image(fetch_image(rootfs), to="rootfs", no_dev=ramdisk)
        populate_rootfs(build_copy_list(cwd, "rootfs", files))
        output_file = os.path.join(cwd, output)
        if ramdisk:
            create_ramdisk(output_file, "rootfs")
        else:
            create_qcow2(output_file, rootfs_size, "rootfs")
        os.chown(output_file, uid, gid)


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "-R",
        "--rootfs",
        help=f"Path to initial root filesystem. This may be a path or URL (default: {DEFAULT_ROOTFS})",
        default=DEFAULT_ROOTFS,
    )
    parser.add_argument("--rootfs-size", help="Size of the root filesystem (default: 250G)", default="250G")
    parser.add_argument("--ramdisk", help="Produce ramdisk image rather than qcow disk image", action="store_true")
    parser.add_argument(
        "-o", "--output", help="Output qcow2 image name (default: rootfs.qcow2)", default="rootfs.qcow2"
    )
    parser.add_argument(
        "--build-dir", help="Explicit file system build directory (will not be cleaned up if specified)", default=None
    )
    parser.add_argument("--uid", help="User id for final output image (internal)", type=int, default=-1)
    parser.add_argument("--gid", help="Group id for final output image (internal)", type=int, default=-1)
    parser.add_argument(
        "files",
        help="Add a single file to the root of the image or a series of files to a single path inside image (file1 file2 .. fileN path)",
        nargs="*",
    )
    return parser.parse_args()


def fetch_image(rootfs):
    if rootfs.startswith("http://"):
        local_file = os.path.basename(rootfs)
        subprocess.check_call(["curl", rootfs, "-o", local_file])
        return local_file
    elif os.path.exists(rootfs):
        return rootfs
    else:
        raise RuntimeError(f"no such file or directory: {rootfs}")


def unpack_image(image, to, no_dev=False):
    if no_dev:
        subprocess.check_call(["tar", "--warning=no-unknown-keyword", "--exclude=dev/*", "-C", to, "-xf", image])
    else:
        subprocess.check_call(["tar", "--warning=no-unknown-keyword", "-C", to, "-xf", image])


def build_copy_list(src_dir, dst_dir, files):
    if not files:
        return []
    elif len(files) == 1:
        files.append(".")

    dst = files.pop()
    if dst == "/":
        dst = "."
    dst = os.path.join(dst_dir, dst)

    copy_list = []

    for src in files:
        if not os.path.isabs(src):
            src = os.path.join(src_dir, src)

        if os.path.isdir(src):
            if src.endswith("/"):
                copy_list.append((src, os.path.join(dst, os.path.basename(src)), distutils.dir_util.copy_tree))
            else:
                copy_list.append((src, dst, distutils.dir_util.copy_tree))
        else:
            copy_list.append((src, dst, distutils.file_util.copy_file))

    return copy_list


def populate_rootfs(copy_list):
    for src, dst, copier in copy_list:
        print(f"COPY {src} -> {dst}")
        copier(src, dst)


def dst_name(file, dst_dir):
    if file.startswith("/"):
        return os.path.join(dst_dir, file[1:])
    else:
        return os.path.join(dst_dir, file)


def create_ramdisk(ramdisk, src_dir):
    with open(ramdisk, "w") as rdfile:
        find = subprocess.Popen(["find", "."], cwd=src_dir, stdout=subprocess.PIPE)
        cpio = subprocess.Popen(
            ["cpio", "--quiet", "-H", "newc", "-o"], cwd=src_dir, stdin=find.stdout, stdout=subprocess.PIPE
        )
        subprocess.check_call(["gzip", "-9", "-n"], stdin=cpio.stdout, stdout=rdfile)
        find.wait()
        cpio.wait()


def create_qcow2(qcow, size, src_dir):
    from subprocess import check_call as cc

    uncompressed_qcow = "rootfs.tmp.qcow"

    try:
        cc(["qemu-img", "create", "-f", "qcow2", "-o", "nocow=on", uncompressed_qcow, size])
        cc(["modprobe", "nbd"])
        # If the `qemu-nbd --connect line here fails with something like the
        # following, you may need to first run the `qemu-nbd --disconnect
        # /dev/nbd0` command (after ensuring noone else is using it!):
        #   qemu-nbd: Failed to set NBD socket
        #   qemu-nbd: Disconnect client, due to: Failed to read request: Unexpected end-of-file before all bytes were read
        cc(["qemu-nbd", "--connect", "/dev/nbd0", uncompressed_qcow])
        cc(["mkfs.ext4", "-d", src_dir, "-L", "rootfs", "/dev/nbd0"])
        cc(["qemu-nbd", "--disconnect", "/dev/nbd0"])
        cc(["qemu-img", "convert", "-O", "qcow2", "-o", "nocow=on", "-c", uncompressed_qcow, qcow])
    finally:
        try:
            os.remove(uncompressed_qcow)
        except FileNotFoundError:
            pass


if __name__ == "__main__":
    args = parse_args()

    if os.getuid() != 0 and not args.ramdisk:
        subprocess.run(
            [
                "sudo",
                "/usr/bin/env",
                f"PYTHONPATH={os.environ['PYTHONPATH']}",
                sys.executable,
                "-m",
                "panthera.tracing.create_image",
                "--uid",
                str(os.getuid()),
                "--gid",
                str(os.getgid()),
            ]
            + sys.argv[1:]
        )
    else:
        run(**vars(args))
