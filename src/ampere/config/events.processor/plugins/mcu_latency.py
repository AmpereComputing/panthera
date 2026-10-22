# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import re

from plugins.common.latency import Latency


def mcu_name(event_name):
    m = re.search("memory_([0-9]+)", event_name)
    if m:
        return f"mcu{m.group(1)}"
    else:
        return event_name


Latency(
    "*.memory_#.place.cmd_queue_insert",
    "*.memory_#.req_completion_time",
    f"{output_dir}/mcu_latency.csv",
    name_col="mcu",
    rename=mcu_name,
)
