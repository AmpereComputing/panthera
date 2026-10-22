# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import sys
from collections import defaultdict

import evp

occ_csv = open("sf_occupancy.csv", "w")
print("time,pha,sf_occupancy", file=occ_csv)


def pha_name(event_name):
    return event_name.rsplit(".", 1)[0]


occupancy = defaultdict(lambda: 0)


def occ(event):
    pha = pha_name(event.name)
    occupancy[pha] += 1
    print(f"{event.time},{pha},{occupancy[pha]}", file=occ_csv)


evp.on("*.p?ha_#.snoop_filter.refill", occ)
