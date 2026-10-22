# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Commit delay by PC"""

melted = df.melt(id_vars=[CONFIGURATION, TRACE, WEIGHT, WORKLOAD])
melted.to_csv("melted.csv")
contains_commit_delay = melted.variable.str.contains("commit_pc.*commit_delay")

melted_commit_delays = melted[contains_commit_delay].dropna()

expanded = melted_commit_delays.variable.str.split("/", expand=True)
pc = expanded.iloc[:, 1].str.replace(".*:", "")
delay = expanded.iloc[:, 2].str.replace(".*:", "").astype(float)
count = melted_commit_delays.value.astype(int)
trace = melted_commit_delays.trace
total_delay = count * delay

combined = pd.DataFrame({"pc": pc, "delay": delay, "count": count, "total_delay": total_delay, TRACE: trace})
print(combined.set_index(TRACE))
print(combined.groupby([TRACE, "pc"]).sum().sort_values("total_delay"))

# create_csv(total_delay, 'commit_delay.csv')
# create_csv(total_delay.sort_values('total_delay'), 'commit_delay_by_delay.csv')
