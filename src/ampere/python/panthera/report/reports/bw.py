# Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Summary of key BW metrics"""

# Bus
metrics = ["sys.soc_0.core_0.bus_bw_rd", "sys.soc_0.core_0.bus_bw_wr"]

means = []
for metric in metrics:
    means.append(metric_report(df, metric=metric))

summary = pd.DataFrame(means)

print("\n==== Bus BW ====")
print(summary)
rup = rollup(df, metrics)
create_plot(rup, "bus_bw.svg", stacked=True)
create_csv(rup, "bus_bw.csv")

# Dcache
metrics = ["sys.soc_0.core_0.dcache.fill_bw", "sys.soc_0.core_0.dcache.read_miss_bw"]

means = []
for metric in metrics:
    means.append(metric_report(df, metric=metric))

summary = pd.DataFrame(means)

print("\n==== Dcache BW ====")
print(summary)
rup = rollup(df, metrics)
create_plot(rup, "dcache_bw.svg", stacked=False)
create_csv(rup, "dcache_bw.csv")

# Icache
metrics = ["sys.soc_0.core_0.icache.fill_bw", "sys.soc_0.core_0.icache.miss_bw"]

means = []
for metric in metrics:
    means.append(metric_report(df, metric=metric))

summary = pd.DataFrame(means)

print("\n==== Icache BW ====")
print(summary)
rup = rollup(df, metrics)
create_plot(rup, "icache_bw.svg", stacked=False)
create_csv(rup, "icache_bw.csv")

# L2 cache
metrics = ["sys.soc_0.core_0.l2cache.fill_bw", "sys.soc_0.core_0.l2cache.read_miss_bw"]

means = []
for metric in metrics:
    means.append(metric_report(df, metric=metric))

summary = pd.DataFrame(means)

print("\n==== L2 cache BW ====")
print(summary)
rup = rollup(df, metrics)
create_plot(rup, "l2cache_bw.svg", stacked=False)
create_csv(rup, "l2cache_bw.csv")
