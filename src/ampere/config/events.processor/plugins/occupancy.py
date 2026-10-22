# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import argparse

from plugins.common.histogram import Histogram
from plugins.common.occupancy import OccupancyHistogram

parser = argparse.ArgumentParser()
parser.add_argument("alloc_event")
parser.add_argument("dealloc_event")
parser.add_argument("metric_name")
parser.add_argument("histogram_settings", nargs="?", type=Histogram.from_spec, default=None)
args = parser.parse_args(evp_args)

OccupancyHistogram(
    args.alloc_event,
    args.dealloc_event,
    args.metric_name,
    histogram_settings=args.histogram_settings,
)
