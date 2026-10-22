# Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Top Down Accounting, Bokeh version"""

import holoviews as hv
import hvplot.pandas
from bokeh.embed import file_html
from bokeh.layouts import column, gridplot
from bokeh.models import (
    BoxZoomTool,
    ColorBar,
    Div,
    HoverTool,
    LinearColorMapper,
    PanTool,
    ResetTool,
    SaveTool,
    WheelZoomTool,
)
from bokeh.palettes import Category20, Viridis256
from bokeh.plotting import ColumnDataSource, figure
from bokeh.resources import Resources

hv.extension("bokeh", "matplotlib")


# TDA groupings
tda_groupings = {
    "Overall (L1)": (["tda.l1.backend", "tda.l1.frontend", "tda.l1.lost", "tda.l1.retired"], "tda.l1.retired", "slots"),
    "Backend (L2)": (["tda.l2.backend_core", "tda.l2.backend_memory"], "tda.l1.backend", "slots"),
    "Frontend (L2)": (["tda.l2.frontend_bandwidth", "tda.l2.frontend_latency"], "tda.l1.frontend", "slots"),
    "Lost (L2)": (["tda.l2.lost_branch_mispredict", "tda.l2.lost_other_clear"], "tda.l1.lost", "slots"),
    "Retired (L2)": (["tda.l2.retired_pipe_utilization"], "tda.l1.retired", "slots"),
    "Backend Memory (L3)": (
        [
            "tda.l3.backend_memory_atomic",
            "tda.l3.backend_memory_dtlb",
            "tda.l3.backend_memory_l1d",
            "tda.l3.backend_memory_l2",
            "tda.l3.backend_memory_mem",
        ],
        "tda.l2.backend_memory",
        "cycles",
    ),
    "Backend Core (L3)": (
        ["tda.l3.backend_core_schedulers", "tda.l3.backend_core_serialize", "tda.l3.backend_core_other"],
        "tda.l2.backend_core",
        "cycles",
    ),
    "Frontend Latency (L3)": (
        [
            "tda.l3.frontend_latency_flow",
            "tda.l3.frontend_latency_l1i",
            "tda.l3.frontend_latency_itlb",
            "tda.l3.frontend_latency_l2",
            "tda.l3.frontend_latency_recovery",
            "tda.l3.frontend_latency_mem",
        ],
        "tda.l2.frontend_latency",
        "cycles",
    ),
    "Frontend Bandwidth (L3)": (
        ["tda.l3.frontend_bandwidth_decoder", "tda.l3.frontend_bandwidth_iq_bw", "tda.l3.frontend_bandwidth_isolate"],
        "tda.l2.frontend_bandwidth",
        "cycles",
    ),
    "Retired Pipe Utilization (L3)": (
        ["tda.l3.retired_pipe_utilization_fsu", "tda.l3.retired_pipe_utilization_ixu"],
        "tda.l2.retired_pipe_utilization",
        "slots",
    ),
    "Backend Core Resource (L4)": (
        [
            "tda.l4.backend_core_resource_int_rf",
            "tda.l4.backend_core_resource_flag_rf",
            "tda.l4.backend_core_resource_bob",
            "tda.l4.backend_core_resource_ixu",
            "tda.l4.backend_core_resource_fp_rf",
            "tda.l4.backend_core_resource_rename",
            "tda.l4.backend_core_resource_fsu",
            "tda.l4.backend_core_resource_rob",
            "tda.l4.backend_core_exec_units",
            "tda.l4.backend_core_resource_lob",
            "tda.l4.backend_core_resource_sob",
            "tda.l4.backend_core_dependancies",
        ],
        "tda.l3.backend_core_other",
        "cycles",
    ),
}

# First, figure out which is our main configuration
configurations = df[CONFIGURATION].unique().tolist()

for i, config in enumerate(configurations):
    print(f"{i}: {config}")
base_idx = int(input("Enter index of configuration you want to use as base: "))

base_config = configurations[base_idx]
idx = infer_index(df)
base_df = df.loc[(df[CONFIGURATION] == base_config)].set_index(idx)

if len(configurations) > 1:
    for i in range(len(configurations)):
        if i == base_idx:
            continue
        ############################################################################
        # Generate top-level scatter combining all L1 metrics into one
        ############################################################################
        print(f'Generating L1 "wind plot"...')

        l1_tda = ["tda.l1.frontend", "tda.l1.backend", "tda.l1.lost", "tda.l1.retired"]
        pivot_df = df.pivot_table(values=l1_tda, index=[idx], columns=[CONFIGURATION])

        comparison_config = configurations[i]
        comparison_df = df.loc[(df[CONFIGURATION] == comparison_config)].set_index(idx)
        for metric in l1_tda:
            comparison_df[f"{metric}_delta"] = comparison_df[metric] - base_df[metric]

        figures = [Div(text=f"<h1>TDA Comparison Plots for {comparison_config} vs. {base_config}<h1>")]

        source = ColumnDataSource(comparison_df)
        color_mapper = LinearColorMapper(
            palette=Viridis256,
            low=comparison_df["tda.l1.retired_delta"].min(),
            high=comparison_df["tda.l1.retired_delta"].max(),
        )

        TOOLTIPS = [
            ("Workload", "@workload"),
            ("Frontend Stalls", "@{tda.l1.frontend} Δ@{tda.l1.frontend_delta}"),
            ("Backend Stalls", "@{tda.l1.backend} Δ@{tda.l1.backend_delta}"),
            ("Lost", "@{tda.l1.lost} Δ@{tda.l1.lost_delta}"),
            ("Retired", "@{tda.l1.retired} Δ@{tda.l1.retired_delta}"),
        ]
        hover = HoverTool(tooltips=TOOLTIPS, name="scatter")
        tools = [hover, WheelZoomTool(), PanTool(), BoxZoomTool(), ResetTool(), SaveTool()]

        p = figure(
            width=900,
            height=800,
            title=f"Change in Frontend/Backend Stall and Retiring Slots from {base_config} to {comparison_config}",
            tools=tools,
        )

        p.xaxis.axis_label = "Frontend Stall Slots"
        p.yaxis.axis_label = "Backend Stall Slots"

        p.line([0, 1], [0, 1], line_width=1, color="grey")
        # Show circles for comparison_config, colored by change in retiring slots from base_config
        p.scatter(
            x=f"tda.l1.frontend",
            y=f"tda.l1.backend",
            name="scatter",
            source=source,
            color={"field": "tda.l1.retired_delta", "transform": color_mapper},
            fill_alpha=0.6,
            size=6,
            legend_group=CONFIGURATION,
        )
        # Line segments from base_config to comparison_config values for frontend and backend stalls for each workload
        p.segment(
            x0=pivot_df["tda.l1.frontend"][base_config],
            y0=pivot_df["tda.l1.backend"][base_config],
            x1=pivot_df["tda.l1.frontend"][comparison_config],
            y1=pivot_df["tda.l1.backend"][comparison_config],
            line_width=2,
            line_alpha=0.3,
            line_color="black",
        )

        cb = ColorBar(color_mapper=color_mapper, title="Change in tda.l1.retired")
        p.add_layout(cb, "right")

        title = Div(text=f'<h2>TDA L1 "Wind Plot"</h2>')
        figures.append(column(title, p))

        ############################################################################
        # Generate grids of scatterplots comparing TDA metrics across configs
        ############################################################################

        hover = HoverTool(
            name="scatter",
            tooltips=[
                (idx, f"@{idx}"),
                (base_config, f"@{base_config}"),
                (comparison_config, f"@{comparison_config}"),
            ],
        )
        tools = [WheelZoomTool(), PanTool(), BoxZoomTool(), ResetTool(), hover]

        all_dfs = {}
        for name, t in tda_groupings.items():
            (metrics, parent, unit) = t
            print(f'Generating comparison plot "{name}"...')

            maximum = 0
            sub_figures = []

            for metric in metrics:
                if metric not in all_dfs:
                    all_dfs[metric] = base_df[[metric]].copy()
                    all_dfs[metric].rename(columns={metric: base_config}, inplace=True)
                    all_dfs[metric] = pd.concat(
                        [all_dfs[metric], comparison_df[[metric]].rename(columns={metric: comparison_config})],
                        axis=1,
                        sort=False,
                    )
                maximum = max(maximum, all_dfs[metric][comparison_config].max())
                maximum = max(maximum, all_dfs[metric][base_config].max())

            i = 0
            for metric in metrics:
                data = {}
                data[comparison_config] = all_dfs[metric][comparison_config]
                data[base_config] = all_dfs[metric][base_config]
                data[idx] = all_dfs[metric][base_config].index.to_series(name=idx)
                source = ColumnDataSource(data)

                p = figure(
                    width=400,
                    height=400,
                    title=metric,
                    x_axis_label=f"{comparison_config} % {unit}",
                    y_axis_label=f"{base_config} % {unit}",
                    tools=tools,
                )
                p.line([0, maximum], [0, maximum], line_width=2, color="grey")
                p.scatter(
                    x=comparison_config,
                    y=base_config,
                    source=source,
                    marker="circle",
                    name="scatter",
                    fill_alpha=0.5,
                    line_alpha=0.5,
                    fill_color=Category20[10 - int(i / 10)][i % 10],
                    line_color=Category20[10 - int(i / 10)][i % 10],
                )
                sub_figures.append(p)
                i += 1

            title = Div(text=f"<h2>TDA {name} Metric Comparisons</h2>")
            g = gridplot(sub_figures, ncols=2, width=400, height=400)
            figures.append(column(title, g))

        print("Generating HTML report...")
        with open(make_filepath(f"tda_comparison_{comparison_config}_vs_{base_config}.html"), "w") as ofile:
            resource_config = Resources(mode="inline")
            ofile.write(
                file_html(figures, resource_config, f"TDA Comparison Plots for {comparison_config} vs. {base_config}")
            )


################################################################################
# Per-workload/suite stacked bar charts for the individual confnigurations
################################################################################

for config in configurations:
    sanitized_config = config.replace("/", "_")
    config_df = df.loc[(df[CONFIGURATION] == config)].set_index([idx])
    figures = [Div(text=f"<h1>TDA Plots for configuration: {config}</h1>")]

    for name, t in tda_groupings.items():
        (metrics, parent, unit) = t
        print(f'Generating plot "{name}"...')

        bd = config_df
        ascending = "retired" in parent
        bd = bd.sort_values(by=parent, ascending=ascending)[metrics]
        fig = bd.hvplot.bar()
        fig.opts(
            title=f"Per-workload {name} TDA Metrics",
            ylabel=f"% of {unit}",
            xlabel=f"{idx} (sorted by {parent})",
            tools=["hover"],
            stacked=True,
            width=1100,
            height=600,
            xrotation=90,
            shared_axes=False,
        )
        figures.append(hv.render(fig))

        bd = self_sort(bd)
        fig = bd.hvplot.line()
        fig.opts(
            title=f"Per-workload {name} TDA Metrics (self-sorted)",
            ylabel=f"% of {unit}",
            xlabel=f"{idx} (self-sorted)",
            tools=["hover"],
            width=1100,
            height=600,
        )
        figures.append(hv.render(fig))

    print("Generating HTML report...")
    with open(make_filepath(f"tda_bokeh.{sanitized_config}.html"), "w") as ofile:
        resource_config = Resources(mode="inline")
        ofile.write(file_html(figures, resource_config, f"TDA Plots for configuration: {config}"))
