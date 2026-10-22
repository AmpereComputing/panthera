# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import re

from plugins.common.occupancy import Occupancy


def inj_name(event_name):
    m = re.search("core_([0-9]+)", event_name)
    if m:
        return f"inj{m.group(1)}"
    else:
        return event_name


Occupancy(
    "*.core_#.txn_id_alloc",
    "*.core_#.txn_id_dealloc",
    f"{output_dir}/fb_occupancy.csv",
    "inj",
    "fb_entries",
    rename=inj_name,
)
