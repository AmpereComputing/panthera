# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from plugins.common.occupancy import Occupancy


def ofb_name(event_name):
    return "OFB"


Occupancy(
    "*.ofb.allocate",
    "*.ofb.deallocate",
    "ofb_occupancy.csv",
    "ofb",
    "LSU OFB occupancy",
    rename=ofb_name,
    report_average=True,
    group_on="phys_addr",
)
