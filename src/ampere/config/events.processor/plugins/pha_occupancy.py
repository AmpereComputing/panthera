# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import argparse

from plugins.common.occupancy import Occupancy


def pha_name(event_name):
    return event_name.rsplit(".", 1)[0]


parser = argparse.ArgumentParser()
parser.add_argument("--output", default="pha_occupancy.csv")
args = parser.parse_args(evp_args)

Occupancy("*.p?ha_#.allocate", "*.p?ha_#.deallocate", args.output, "pha", "tracker occupancy", rename=pha_name)
