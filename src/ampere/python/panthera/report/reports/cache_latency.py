# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Report of cache refill latencies"""

metrics = ["itlb_refill_lat", "dtlb_refill_lat", "l1i_cache_refill_lat", "l1d_cache_refill_lat"]

latencies = rollup(df, metrics=metrics).reset_index()
latencies.set_index(TRACE, inplace=True)

create_plot(latencies, "cache_latencies.svg")
create_csv(latencies, "cache_latencies.csv")
