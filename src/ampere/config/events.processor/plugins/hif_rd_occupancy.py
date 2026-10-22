# Copyright (c) 2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import argparse

from plugins.common.occupancy import Occupancy


def mem_name(event_name):
    return event_name.rsplit(".", 1)[0]


parser = argparse.ArgumentParser()
parser.add_argument("--output", default="hif_rd_occupancy.csv")
args = parser.parse_args(evp_args)

Occupancy(
    "*.memory_#.hif_rd.acquire", "*.memory_#.hif_rd.release", args.output, "mem", "hif_rd occupancy", rename=mem_name
)
