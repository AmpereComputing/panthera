# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from plugins.common.event_histogram import EventHistogramPerKey
from plugins.common.histogram import Histogram

# How many triggers/launches are done "per-stream" (i.e. per entry)
EventHistogramPerKey(
    "per_stream",
    ["*.pcp.trigger", "*.pcp.launch"],
    key_data_types=["pcp_index"],
    stop_event="*.stride_mult_rst",
    histogram_settings=Histogram(maximum=512, exponential_bucket_size=2),
)
