# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import argparse

from plugins.common.occupancy import Occupancy


def mem_name(event_name):
    return event_name.rsplit(".", 1)[0]


parser = argparse.ArgumentParser()
parser.add_argument("--output", default="mem_occupancy.csv")
args = parser.parse_args(evp_args)

Occupancy(
    "*.memory_#.chp.retry.alloc",
    "*.memory_#.chp.retry.dealloc",
    args.output,
    "mem",
    "memory occupancy",
    rename=mem_name,
)
