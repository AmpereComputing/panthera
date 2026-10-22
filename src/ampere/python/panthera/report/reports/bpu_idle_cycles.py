# Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""BPU idle cycles"""

melted = df.melt(id_vars=[TRACE, CONFIGURATION])
melted.to_csv("melted.csv")

contains_idle_cycles = melted.variable.str.contains("pc_mux.*idle_cycles")

melted_idle_cycles = melted[contains_idle_cycles].dropna()

expanded = melted_idle_cycles.variable.str.split("/", expand=True)
cycles = expanded.iloc[:, 1].str.replace(".*:", "").astype(int)
count = melted_idle_cycles.value.astype(int)

combined = pd.DataFrame({CONFIGURATION: melted_idle_cycles[CONFIGURATION], "cycles": cycles, "count": count})

idle_cycles = combined.groupby([CONFIGURATION, "cycles"]).sum().sort_values("cycles").reset_index()
idle_cycles = idle_cycles.pivot(index="cycles", columns=CONFIGURATION, values="count")
create_csv(idle_cycles, "bpu_idle_cycles.csv")

create_plot(idle_cycles.iloc[0:29, :], "bpu_idle_cycles_top_30.svg")
create_plot(idle_cycles.iloc[2:31, :], "bpu_idle_cycles_2_31.svg")
