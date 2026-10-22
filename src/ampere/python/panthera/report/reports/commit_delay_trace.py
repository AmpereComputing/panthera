# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Commit delay by PC"""

import os
import re

import pandas as pd

series = df.sum()

contains_commit_delay = series.index.str.contains("commit_pc.*commit_delay")
commit_delays = pd.DataFrame(series.loc[contains_commit_delay].sort_values()).reset_index()

expanded = commit_delays.iloc[:, 0].str.split("/", expand=True)

pc = expanded.iloc[:, 1].str.replace(".*:", "").astype(str).apply(int).apply(hex)
delay = expanded.iloc[:, 2].str.replace(".*:", "").astype(int)
count = commit_delays.iloc[:, -1].astype(int)
total_delay = count * delay

combined = pd.DataFrame({"pc": pc, "count": count, "total_delay": total_delay})

delay_by_pc = combined.groupby("pc").sum()
delay_by_pc.loc[:, "cpi"] = delay_by_pc.loc[:, "total_delay"] / delay_by_pc.loc[:, "count"]
delay_by_pc.sort_values("total_delay", inplace=True)

biggest_delays = delay_by_pc.tail(100)
biggest_delays_by_cpi = biggest_delays.sort_values("cpi")

basename = os.path.basename(input_file)
rootname = re.sub("(.ts)*(.csv)*(.gz)*", "", basename)

plot_df = biggest_delays_by_cpi.loc[:, ["total_delay", "cpi"]].tail(25)
create_plot(
    plot_df,
    f"{rootname}.commit_delay.svg",
    title=f"Commit Delay by PC for {rootname}",
    y_label="Cycles",
    secondary_y=["cpi"],
    secondary_y_label="Cycles per Instruction",
    kind="bar",
)
create_csv(delay_by_pc, f"{rootname}.commit_delay.csv")

print(biggest_delays_by_cpi.tail(25))
