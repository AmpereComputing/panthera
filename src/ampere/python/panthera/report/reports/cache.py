# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Core cache metrics"""

import pandas as pd

cache_metrics = [
    "sys.soc_0.core_0.icf.cache.hit_rate",
    "sys.soc_0.core_0.lsu.l1d_cache.hit_rate",
    "sys.soc_0.core_0.l2cache.hit_rate",
    "sys.soc_0.core_0.icache.mpki",
    "sys.soc_0.core_0.dcache.mpki",
    "sys.soc_0.core_0.l2cache.mpki",
]

means = []
for metric in cache_metrics:
    means.append(metric_report(df, metric=metric))

summary = pd.DataFrame(means)
print(summary)

cache_stats = rollup(df, cache_metrics)
create_csv(cache_stats, "cache_stats.csv")
