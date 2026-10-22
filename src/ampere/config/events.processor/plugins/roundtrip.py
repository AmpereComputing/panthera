# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import re

from plugins.common.latency import Latency


def core_name(event_name):
    m = re.search("core_([0-9]+)", event_name)
    if m:
        return f"inj{m.group(1)}"
    else:
        return event_name


Latency("*.core_#.send_req", "*.core_#.recv_dat", "roundtrip_latency.csv", name_col="inj_latency", rename=core_name)
