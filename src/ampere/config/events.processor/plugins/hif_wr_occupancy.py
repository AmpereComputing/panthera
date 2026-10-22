# Copyright (c) 2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import argparse

from plugins.common.occupancy import Occupancy


def mem_name(event_name):
    return event_name.rsplit(".", 1)[0]


parser = argparse.ArgumentParser()
parser.add_argument("--output", default="hif_wr_occupancy.csv")
args = parser.parse_args(evp_args)

Occupancy(
    "*.memory_#.hif_wr.acquire", "*.memory_#.hif_wr.release", args.output, "mem", "hif_wr occupancy", rename=mem_name
)
