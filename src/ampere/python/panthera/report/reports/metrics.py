# Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Core PMU metrics"""

import pandas as pd

metrics = [
    "sys.soc_0.core_0.ipc",
    "sys.soc_0.core_0.ips",
    "sys.soc_0.core_0.ops_retired",
    "sys.soc_0.core_0.ops_lost",
    "sys.soc_0.core_0.cpu_utilization",
    "sys.soc_0.core_0.cpu_lost",
    "sys.soc_0.core_0.branch.mispredict.pki",
    "sys.soc_0.core_0.branch.mispredict.ratio",
    "sys.soc_0.core_0.icf.itlb.hit_rate",
    "sys.soc_0.core_0.itlb.mpki",
    "sys.soc_0.core_0.icf.cache.hit_rate",
    "sys.soc_0.core_0.icache.mpki",
    "sys.soc_0.core_0.icf.cache.long.hit_rate",
    "sys.soc_0.core_0.icache.lmpki",
    "sys.soc_0.core_0.lsu.dl1_tlb.hit_rate",
    "sys.soc_0.core_0.dtlb.mpki",
    "sys.soc_0.core_0.lsu.l1d_cache.hit_rate",
    "sys.soc_0.core_0.dcache.mpki",
    "sys.soc_0.core_0.lsu.l1d_cache.read.hit_rate",
    "sys.soc_0.core_0.dcache.lmpki",
    "sys.soc_0.core_0.l2cache.hit_rate",
    "sys.soc_0.core_0.l2cache.read.hit_rate",
    "sys.soc_0.core_0.l2cache.mpki",
    "sys.soc_0.core_0.l2cache.lmpki",
    "sys.soc_0.core_0.immu.miss_rate",
    "sys.soc_0.core_0.immu.hit_rate",
    "sys.soc_0.core_0.immu.mpki",
    "sys.soc_0.core_0.dmmu.miss_rate",
    "sys.soc_0.core_0.dmmu.hit_rate",
    "sys.soc_0.core_0.dmmu.mpki",
    "sys.soc_0.core_0.dtlb_walk_rate",
    "sys.soc_0.core_0.itlb_walk_rate",
    "top.mesh.slc.array.mpki",
    "top.mesh.slc.array.hit_rate",
    "sys.soc_0.core_0.load_mix",
    "sys.soc_0.core_0.store_mix",
    "sys.soc_0.core_0.load_store_mix",
    "sys.soc_0.core_0.integer_mix",
    "sys.soc_0.core_0.ase_mix",
    "sys.soc_0.core_0.vfp_mix",
    "sys.soc_0.core_0.pc_write_mix",
    "sys.soc_0.core_0.crypto_mix",
    "sys.soc_0.core_0.stalls_pki",
    "sys.soc_0.core_0.stalls_frontend_pki",
    "sys.soc_0.core_0.stalls_backend_pki",
    "sys.soc_0.core_0.stall_rate",
    "sys.soc_0.core_0.stall_frontend_rate",
    "sys.soc_0.core_0.stall_backend_rate",
    "sys.soc_0.core_0.stall_backend_mem_rate",
]

means = []
for metric in metrics:
    means.append(metric_report(df, metric=metric))

summary = pd.DataFrame(means)
print(summary)

stats = rollup(df, metrics)
create_csv(stats, "metrics.csv")
