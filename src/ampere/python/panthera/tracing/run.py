#!/usr/bin/env python3
# Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Run QEMU.

This is fairly limited in the way it runs QEMU today, but is at least
useful for running/tracing simple benchmarks.
"""

import shlex
import subprocess
import sys

from . import arg_list_to_dict, dict_to_arg_list


def run(**kwargs):
    init = kwargs.pop("init", None)
    init_args = kwargs.pop("init_args", None)
    dry_run = kwargs.pop("dry_run", False)

    arguments = {
        "smp": 1,
        "m": "4G",
        "M": "virt",
        "cpu": "cortex-a57",
        "nographic": True,
        "semihosting": True,
        "icount": 0,
        "kernel": "Image-64k-v5.18",  # on command line specify: -kernel PATH-TO-YOUR-COPY/Image-64k-v5.18
        "append": ["root=/dev/vda"],
    }

    arguments.update(kwargs)

    if init:
        arguments["append"].append(f"init={init}")

    if init_args:
        arguments["append"].append("--")
        if isinstance(init_args, list):
            arguments["append"].extend(init_args)
        else:
            arguments["append"].append(init_args)

    command = ["qemu-system-aarch64"] + dict_to_arg_list(arguments)

    command_str = " ".join(shlex.quote(arg) for arg in command)
    if dry_run:
        print(f"would run:\n\t{command_str}")
    else:
        print(f"running:\n\t{command_str}")
        subprocess.run(command)


if __name__ == "__main__":
    run(**arg_list_to_dict(sys.argv))
