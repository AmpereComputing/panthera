# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""IXU pipe usage"""

import re

pipe_map = {
    "sys.soc_0.core_0.lsu.uop_ld_execute": "SchC",
    "sys.soc_0.core_0.lsu.uop_sta_execute": "SchD",
}

pipes = list(iter(pipe_map))
pipe_metrics = list(df.filter(regex=r"^.*?\.xu\..*?\.uop_execute").columns)
for metric in pipes:
    if metric in df.columns:
        pipe_metrics.append(metric)


r = rollup(df, metrics=pipe_metrics)
r = r.rename(columns=lambda c: pipe_map.get(c, re.sub(r"^.*?\.xu\.(.*?)\.uop_execute", r"Sch\1", c)))

r.insert(0, "SchA", r.loc[:, "SchA_P0"] + r.loc[:, "SchA_P1"])
r.insert(1, "SchB", r.loc[:, "SchB_P0"] + r.loc[:, "SchB_P1"])

create_plot(r, "pipe_usage.svg")

ab = r[["SchA", "SchB"]]
create_plot(ab, "pipe_usage_ab.svg")
xy = r[["SchX", "SchY"]]
create_plot(xy, "pipe_usage_xy.svg")

print(r.mean())
