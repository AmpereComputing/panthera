# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Top-Down Accounting Sunburst Plots"""

import glob
import os

import plotly.express as px
from PIL import Image

df = df.filter(regex=f"(tda|{TRACE}|{WORKLOAD}|{CONFIGURATION})")
dfs = [x for _, x in df.groupby(CONFIGURATION)]

if not os.path.exists("images"):
    os.mkdir("images")

# Generate plots for each configuration
for df in dfs:
    config = df[CONFIGURATION].loc[df.index[0]]
    df_mean = pd.DataFrame({""})

    parent_map = [
        # TDA metric        # Parent metric     # Mean value                                                # TDA metric description
        ["fe", "TDA", df["tda.l1.frontend"].mean() * 100, "Frontend stalled slots"],
        [
            "fe_latency",
            "fe",
            df["tda.l2.frontend_latency"].mean() * 100,
            "Frontend latency stall (cycles 0 slots dispatched)",
        ],
        ["fe_itlb_miss", "fe_latency", df["tda.l3.frontend_latency_itlb"].mean() * 100, "ITLB access latency"],
        ["fe_l1i_latency", "fe_latency", df["tda.l3.frontend_latency_l1i"].mean() * 100, "L1I cache latency"],
        ["fe_l2_latency", "fe_latency", df["tda.l3.frontend_latency_l2"].mean() * 100, "L2 cache latency"],
        [
            "fe_mem_latency",
            "fe_latency",
            df["tda.l3.frontend_latency_mem"].mean() * 100,
            "Non-core cache/memory latency",
        ],
        [
            "recovery",
            "fe_latency",
            df["tda.l3.frontend_latency_recovery"].mean() * 100,
            "Dispatch stalls due to flushes",
        ],
        [
            "flow",
            "fe_latency",
            df["tda.l3.frontend_latency_flow"].mean() * 100,
            "Dispatch stalls due to unavailability of fetch PCs from BPU",
        ],
        ["btc_clear", "flow", df["tda.l4.frontend_btc_clear"].mean() * 100, "Dispatch stalls due to BTC clear"],
        ["btb_clear", "flow", df["tda.l4.frontend_btb_clear"].mean() * 100, "Dispatch stalls due to BTB clear"],
        [
            "fe_bw",
            "fe",
            df["tda.l2.frontend_bandwidth"].mean() * 100,
            "Frontend Bandwidth Stall (cycles less than cpu-width slots dispatched)",
        ],
        [
            "isolates",
            "fe_bw",
            df["tda.l3.frontend_bandwidth_isolate"].mean() * 100,
            "Slots wasted due to isolate dispatches",
        ],
        [
            "iq_bw",
            "fe_bw",
            df["tda.l3.frontend_bandwidth_iq_bw"].mean() * 100,
            "Slots wasted due to no more instructions in IQ",
        ],
        [
            "decoder",
            "fe_bw",
            df["tda.l3.frontend_bandwidth_decoder"].mean() * 100,
            "Slots wasted due to decoder limitations",
        ],
        ["be", "TDA", df["tda.l1.backend"].mean() * 100, "Backend stalled slots"],
        [
            "be_mem",
            "be",
            df["tda.l2.backend_memory"].mean().mean() * 100,
            "Backend stalled, next-to-commit is a memory uop",
        ],
        ["be_dtlb_miss", "be_mem", df["tda.l3.backend_memory_dtlb"].mean() * 100, "DTLB access latency"],
        ["be_l1d_latency", "be_mem", df["tda.l3.backend_memory_l1d"].mean() * 100, "L1D cache latency"],
        ["ld_on_st", "be_l1d_latency", df["tda.l4.stall_backend_ld_on_st"].mean() * 100, "ST -> LD forwarding latency"],
        ["be_l2_latency", "be_mem", df["tda.l3.backend_memory_l2"].mean() * 100, "L2 cache latency"],
        [
            "ofb_full",
            "be_l2_latency",
            df["tda.l4.stall_backend_ofb_full"].mean() * 100,
            "L2 cache latency due to unavailibility of OFB entries",
        ],
        [
            "l2_fbfull_sched",
            "be_l2_latency",
            df["tda.l4.l2c_fb_full_sched_stall"].mean() * 100,
            "L2 scheduling stalled due to unavailibility of FB entries",
        ],
        ["be_mem_latency", "be_mem", df["tda.l3.backend_memory_mem"].mean() * 100, "Non-core cache/memory latency"],
        [
            "atomic_latency",
            "be_mem",
            df["tda.l3.backend_memory_atomic"].mean() * 100,
            "Backend stalled, next-to-commit is an atomic",
        ],
        [
            "be_core",
            "be",
            df["tda.l2.backend_core"].mean() * 100,
            "Backend stalled due to unavailability of core resource (and next-to-commit is not a memory uop)",
        ],
        [
            "serialize",
            "be_core",
            df["tda.l3.backend_core_serialize"].mean() * 100,
            "Backend stalled due to dispatch of a serializing (post-dispatch or pre-dispatch draining) uop",
        ],
        ["schedulers", "be_core", df["tda.l3.backend_core_schedulers"].mean() * 100, "Scheduler entries unavailable"],
        ["ixu", "schedulers", df["tda.l4.backend_core_resource_ixu"].mean() * 100, "IXU scheduler entries unavailable"],
        [
            "busy_p0a",
            "ixu",
            df["tda.l4.backend_core_busy_p0a"].mean() * 100,
            "No issues due to P0_A scheduler execution unit being busy",
        ],
        [
            "busy_p0b",
            "ixu",
            df["tda.l4.backend_core_busy_p0b"].mean() * 100,
            "No issues due to P0_B scheduler execution unit being busy",
        ],
        [
            "busy_p0j",
            "ixu",
            df["tda.l4.backend_core_busy_p0j"].mean() * 100,
            "No issues due to P0_J scheduler execution unit being busy",
        ],
        [
            "busy_p0k",
            "ixu",
            df["tda.l4.backend_core_busy_p0k"].mean() * 100,
            "No issues due to P0_K scheduler execution unit being busy",
        ],
        [
            "busy_p0m",
            "ixu",
            df["tda.l4.backend_core_busy_p0m"].mean() * 100,
            "No issues due to P0_M scheduler execution unit being busy",
        ],
        [
            "busy_p0n",
            "ixu",
            df["tda.l4.backend_core_busy_p0n"].mean() * 100,
            "No issues due to P0_N scheduler execution unit being busy",
        ],
        [
            "busy_p1a",
            "ixu",
            df["tda.l4.backend_core_busy_p1a"].mean() * 100,
            "No issues due to P1_A scheduler execution unit being busy",
        ],
        [
            "busy_p1b",
            "ixu",
            df["tda.l4.backend_core_busy_p1b"].mean() * 100,
            "No issues due to P1_B scheduler execution unit being busy",
        ],
        [
            "busy_p1j",
            "ixu",
            df["tda.l4.backend_core_busy_p1j"].mean() * 100,
            "No issues due to P1_J scheduler execution unit being busy",
        ],
        [
            "busy_p1k",
            "ixu",
            df["tda.l4.backend_core_busy_p1k"].mean() * 100,
            "No issues due to P1_K scheduler execution unit being busy",
        ],
        [
            "busy_p1m",
            "ixu",
            df["tda.l4.backend_core_busy_p1m"].mean() * 100,
            "No issues due to P1_M scheduler execution unit being busy",
        ],
        [
            "busy_p1n",
            "ixu",
            df["tda.l4.backend_core_busy_p1n"].mean() * 100,
            "No issues due to P1_N scheduler execution unit being busy",
        ],
        [
            "ilock_p0a",
            "ixu",
            df["tda.l4.backend_core_ilock_p0a"].mean() * 100,
            "No issues due to P0_A scheduler entries not being ready",
        ],
        [
            "ilock_p0b",
            "ixu",
            df["tda.l4.backend_core_ilock_p0b"].mean() * 100,
            "No issues due to P0_B scheduler entries not being ready",
        ],
        [
            "ilock_p0j",
            "ixu",
            df["tda.l4.backend_core_ilock_p0j"].mean() * 100,
            "No issues due to P0_J scheduler entries not being ready",
        ],
        [
            "ilock_p0k",
            "ixu",
            df["tda.l4.backend_core_ilock_p0k"].mean() * 100,
            "No issues due to P0_K scheduler entries not being ready",
        ],
        [
            "ilock_p0m",
            "ixu",
            df["tda.l4.backend_core_ilock_p0m"].mean() * 100,
            "No issues due to P0_M scheduler entries not being ready",
        ],
        [
            "ilock_p0n",
            "ixu",
            df["tda.l4.backend_core_ilock_p0n"].mean() * 100,
            "No issues due to P0_N scheduler entries not being ready",
        ],
        [
            "ilock_p1a",
            "ixu",
            df["tda.l4.backend_core_ilock_p1a"].mean() * 100,
            "No issues due to P1_A scheduler entries not being ready",
        ],
        [
            "ilock_p1b",
            "ixu",
            df["tda.l4.backend_core_ilock_p1b"].mean() * 100,
            "No issues due to P1_B scheduler entries not being ready",
        ],
        [
            "ilock_p1j",
            "ixu",
            df["tda.l4.backend_core_ilock_p1j"].mean() * 100,
            "No issues due to P1_J scheduler entries not being ready",
        ],
        [
            "ilock_p1k",
            "ixu",
            df["tda.l4.backend_core_ilock_p1k"].mean() * 100,
            "No issues due to P1_K scheduler entries not being ready",
        ],
        [
            "ilock_p1m",
            "ixu",
            df["tda.l4.backend_core_ilock_p1m"].mean() * 100,
            "No issues due to P1_M scheduler entries not being ready",
        ],
        [
            "ilock_p1n",
            "ixu",
            df["tda.l4.backend_core_ilock_p1n"].mean() * 100,
            "No issues due to P1_N scheduler entries not being ready",
        ],
        ["fsu", "schedulers", df["tda.l4.backend_core_resource_fsu"].mean() * 100, "FSU scheduler entries unavailable"],
        [
            "busy_p0x",
            "fsu",
            df["tda.l4.backend_core_busy_p0x"].mean() * 100,
            "No issues due to P0_X scheduler execution unit being busy",
        ],
        [
            "busy_p0y",
            "fsu",
            df["tda.l4.backend_core_busy_p0y"].mean() * 100,
            "No issues due to P0_Y scheduler execution unit being busy",
        ],
        [
            "busy_p1x",
            "fsu",
            df["tda.l4.backend_core_busy_p1x"].mean() * 100,
            "No issues due to P1_X scheduler execution unit being busy",
        ],
        [
            "busy_p1y",
            "fsu",
            df["tda.l4.backend_core_busy_p1y"].mean() * 100,
            "No issues due to P1_Y scheduler execution unit being busy",
        ],
        [
            "ilock_p0x",
            "fsu",
            df["tda.l4.backend_core_ilock_p0x"].mean() * 100,
            "No issues due to P0_X scheduler entries not being ready",
        ],
        [
            "ilock_p0y",
            "fsu",
            df["tda.l4.backend_core_ilock_p0y"].mean() * 100,
            "No issues due to P0_Y scheduler entries not being ready",
        ],
        [
            "ilock_p1x",
            "fsu",
            df["tda.l4.backend_core_ilock_p1x"].mean() * 100,
            "No issues due to P1_X scheduler entries not being ready",
        ],
        [
            "ilock_p1y",
            "fsu",
            df["tda.l4.backend_core_ilock_p1y"].mean() * 100,
            "No issues due to P1_Y scheduler entries not being ready",
        ],
        [
            "other",
            "be_core",
            df["tda.l3.backend_core_other"].mean() * 100,
            "Core resources unavailable (except schedulers)",
        ],
        [
            "rename",
            "other",
            df["tda.l4.backend_core_resource_rename"].mean() * 100,
            "Rename resources (PRF) unavailable",
        ],
        ["int_rf", "rename", df["tda.l4.backend_core_resource_int_rf"].mean() * 100, "Integer PRF unavailable"],
        ["fp_rf", "rename", df["tda.l4.backend_core_resource_fp_rf"].mean() * 100, "FP PRF unavailable"],
        ["flag_rf", "rename", df["tda.l4.backend_core_resource_flag_rf"].mean() * 100, "Flag PRF unavailable"],
        ["rob", "other", df["tda.l4.backend_core_resource_rob"].mean() * 100, "ROB entries unavailable"],
        ["sob", "other", df["tda.l4.backend_core_resource_sob"].mean() * 100, "SOB entries unavailable"],
        ["lob", "other", df["tda.l4.backend_core_resource_lob"].mean() * 100, "LOB entries unavailable"],
        ["bob", "other", df["tda.l4.backend_core_resource_bob"].mean() * 100, "BOB entries unavailable"],
        ["retired", "TDA", df["tda.l1.retired"].mean() * 100, "Retired slots"],
        [
            "pipe_util",
            "retired",
            df["tda.l2.retired_pipe_utilization"].mean() * 100,
            "Pipe utilization - percentage of issue-slots utilized",
        ],
        [
            "ixu_util",
            "pipe_util",
            df["tda.l3.retired_pipe_utilization_ixu"].mean() * 100,
            "IXU scheduler utilization - percentage of IXU issue-slots utilized",
        ],
        [
            "ixu_util_p0a",
            "ixu_util",
            df["tda.l4.retired_pipe_utilization_ixu_p0a"].mean() * 100,
            "uops issued from P0_A scheduler",
        ],
        [
            "ixu_util_p0b",
            "ixu_util",
            df["tda.l4.retired_pipe_utilization_ixu_p0b"].mean() * 100,
            "uops issued from P0_B scheduler",
        ],
        [
            "ixu_util_p0j",
            "ixu_util",
            df["tda.l4.retired_pipe_utilization_ixu_p0j"].mean() * 100,
            "uops issued from P0_J scheduler",
        ],
        [
            "ixu_util_p0k",
            "ixu_util",
            df["tda.l4.retired_pipe_utilization_ixu_p0k"].mean() * 100,
            "uops issued from P0_K scheduler",
        ],
        [
            "ixu_util_p0m",
            "ixu_util",
            df["tda.l4.retired_pipe_utilization_ixu_p0m"].mean() * 100,
            "uops issued from P0_M scheduler",
        ],
        [
            "ixu_util_p0n",
            "ixu_util",
            df["tda.l4.retired_pipe_utilization_ixu_p0n"].mean() * 100,
            "uops issued from P0_N scheduler",
        ],
        [
            "ixu_util_p1a",
            "ixu_util",
            df["tda.l4.retired_pipe_utilization_ixu_p1a"].mean() * 100,
            "uops issued from P1_A scheduler",
        ],
        [
            "ixu_util_p1b",
            "ixu_util",
            df["tda.l4.retired_pipe_utilization_ixu_p1b"].mean() * 100,
            "uops issued from P1_B scheduler",
        ],
        [
            "ixu_util_p1j",
            "ixu_util",
            df["tda.l4.retired_pipe_utilization_ixu_p1j"].mean() * 100,
            "uops issued from P1_J scheduler",
        ],
        [
            "ixu_util_p1k",
            "ixu_util",
            df["tda.l4.retired_pipe_utilization_ixu_p1k"].mean() * 100,
            "uops issued from P1_K scheduler",
        ],
        [
            "ixu_util_p1m",
            "ixu_util",
            df["tda.l4.retired_pipe_utilization_ixu_p1m"].mean() * 100,
            "uops issued from P1_M scheduler",
        ],
        [
            "ixu_util_p1n",
            "ixu_util",
            df["tda.l4.retired_pipe_utilization_ixu_p1n"].mean() * 100,
            "uops issued from P1_N scheduler",
        ],
        [
            "fsu_util",
            "pipe_util",
            df["tda.l3.retired_pipe_utilization_fsu"].mean() * 100,
            "FSU scheduler utilization - percentage of FSU issue-slots utilized",
        ],
        [
            "fsu_util_p0x",
            "fsu_util",
            df["tda.l4.retired_pipe_utilization_fsu_p0x"].mean() * 100,
            "uops issued from P0_X scheduler",
        ],
        [
            "fsu_util_p0y",
            "fsu_util",
            df["tda.l4.retired_pipe_utilization_fsu_p0y"].mean() * 100,
            "uops issued from P0_Y scheduler",
        ],
        [
            "fsu_util_p1x",
            "fsu_util",
            df["tda.l4.retired_pipe_utilization_fsu_p1x"].mean() * 100,
            "uops issued from P1_X scheduler",
        ],
        [
            "fsu_util_p1y",
            "fsu_util",
            df["tda.l4.retired_pipe_utilization_fsu_p1y"].mean() * 100,
            "uops issued from P1_Y scheduler",
        ],
        ["lost", "TDA", df["tda.l1.lost"].mean() * 100, "Slots dispatched, but not retired (flushed)"],
        [
            "br_mispred",
            "lost",
            df["tda.l2.lost_branch_mispredict"].mean() * 100,
            "Slots lost due to branch mispredictions",
        ],
        [
            "ind",
            "br_mispred",
            df["tda.l3.lost_branch_mispredict_ind"].mean() * 100,
            "Slots lost due to indirect branch mispredictions",
        ],
        [
            "ret",
            "br_mispred",
            df["tda.l3.lost_branch_mispredict_ret"].mean() * 100,
            "Slots lost due to RET branch mispredictions",
        ],
        [
            "imm",
            "br_mispred",
            df["tda.l3.lost_branch_mispredict_imm"].mean() * 100,
            "Slots lost due to conditional (immediate) branch mispredictions",
        ],
        [
            "other_clears",
            "lost",
            df["tda.l2.lost_other_clear"].mean() * 100,
            "Slots lost due to non-branch related flushes",
        ],
    ]

    df_tda = pd.DataFrame(parent_map, columns=["metric", "parent", "value", "description"])

    # The following for-loops generate the a new 'adjusted_value' column data, which will be used for plotting, and is based on the existing 'value' (TDA metric means) data.
    # The new column's sum of all children metrics are equal-to or less-than the parent metric, and proportional to the individual 'value' column data (if adjusted). This
    # condition is a requirement of the plotly functions.

    # Recursive passes (for-loops), equal to the max depth of the TDA metrics, are required to ensure 'adjusted_value' is generated correctly.
    df_tda["adjusted_value"] = df_tda["value"]
    # deepest TDA metric level
    tda_depth = 4

    for i in range(tda_depth):
        for index, row in df_tda.iterrows():
            child_sum = df_tda.loc[df_tda["parent"] == row["metric"], "value"].sum()
            if child_sum > row["adjusted_value"]:
                df_tda.loc[df_tda["parent"] == row["metric"], "adjusted_value"] = df_tda.loc[
                    df_tda["parent"] == row["metric"], "value"
                ] * (row["adjusted_value"] / child_sum)

    # Sort dataframe to pin the l1.retired slice
    df_tda = df_tda.sort_values(by="metric", ascending=False)
    fig = px.sunburst(
        df_tda,
        names="metric",
        parents="parent",
        values="adjusted_value",
        branchvalues="total",
        hover_data="description",
        height=1500,
        width=1500,
    )
    fig.update_layout(title_text=config, title_font_size=30, title_x=0.5)
    fig.update_traces(textinfo="label+percent root")
    # Disable sort to prevent level1 sorting based on slice size (plotly default)
    fig.update_traces(sort=False)

    fig.write_html("tda_" + config + ".html")
    fig.write_image("images/fig_" + config + ".png")

images = []
for filename in sorted(glob.glob("images/*.png")):
    im = Image.open(filename)
    im_small = im.resize((1500, 1500), resample=0)
    images.append(im_small)

last_frame = len(images)
print(len(images))

for x in range(0, 1):
    im = images[last_frame - 1]
    images.append(im)

images[0].save("tda_shift.gif", save_all=True, append_images=images[1:], optimize=False, duration=500, loop=0)
