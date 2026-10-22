# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

# this plugin can only be used if warmup is disabled

from plugins.common.occupancy import Occupancy


def core_name(event_name):
    return event_name.rsplit(".", 1)[0]


Occupancy(
    "*.core_#.cache.fill", "*.core_#.cache.invalidate", "l2_occupancy.csv", "core", "l2 occupancy", rename=core_name
)
