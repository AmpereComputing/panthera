#!/usr/bin/env python3
# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Create a pivot table in an output excel file and plot speedup charts

You can specify metrics to pivot one per line in a file
via `--include-file`, or simply add them as positional arguments on the
command-line with `--include`.

Also you can choose the baseline configuration if you use -c argument

"""

import argparse
import os.path
import re

import bokeh
import holoviews as hv
import hvplot.pandas
import numpy as np
import openpyxl
import pandas as pd
import panel as pn
import panthera.report.helpers
import scipy
from bokeh.models import GlyphRenderer, LinearAxis, LinearScale, Range1d
from holoviews import opts
from panthera.rollup import CONFIGURATION

bokeh.core.validation.silence(bokeh.core.validation.warnings.FIXED_SIZING_MODE, True)

pn.extension(template="material")

page_width = 1200
min_ipc = 0
max_ipc = 1


def speedup(df, metric, index):
    """Rollup the speedup of 'metric' for all of the configurations.
    The speedup is the ratio of the metric value for a configuration divided by the value in the base configuration
    which is assumed to be the first configuration. The function subtracts 1 from the ratio so that positive values
    indicate a speedup and negative values indicate a slowdown.
    :param df: dataframe
    :param metric: metric to rollup
    :return: dataframe
    """
    idx = panthera.report.helpers.infer_index(df)
    configurations = list(dict.fromkeys(df[CONFIGURATION]))

    base = configurations[index]

    ndf = df.reindex([idx, CONFIGURATION, metric], axis="columns")
    metric_table = ndf.pivot_table(index=idx, columns=CONFIGURATION, values=metric)
    metric_table = metric_table.reindex(configurations, axis="columns")
    su = metric_table.div(metric_table[base], axis=0)
    return su


def make_filepath(filename, pretex, idx):
    """Make a filepath based on the output directory, rollup grouping and filename
    :param filename: basename of the file to be created
    :return: full path of file to be created
    """
    head_tail = os.path.split(filename)
    (basename, ext) = os.path.splitext(head_tail[1])
    output_dir = head_tail[0]
    return os.path.join(output_dir, f"{pretex}{basename}.by_{idx}{ext}")


def create_trend_chart(df, title="", xlabel=None, ylabel=None, width=page_width):
    chart = df.hvplot.line()
    chart.opts(
        title=title,
        tools=["hover", "vline"],
        show_grid=True,
        shared_axes=False,
        width=width,
        ylabel=ylabel,
        xlabel=xlabel,
        xticks=None,
        xaxis=None,
    )
    return chart


def ipc_geomean_and_chart(df, metrics, container):
    # manually add more indices to be dropped here
    indices_to_drop = [base_index]
    for i in indices_to_drop:
        selected_configs.remove(configurations[i])

    for metric, label in metrics.items():
        idx = panthera.report.helpers.infer_index(df)
        base = configurations[base_index]
        ipc_ratio = speedup(df, metric, base_index)
        ipc_ratio.sort_values(by=configurations[sort_idx], inplace=True)
        ipc = df.loc[:, [CONFIGURATION, idx, metric]]
        ipc_m = df.pivot_table(index=idx, columns=CONFIGURATION, values=metric)
        ipc_m[configurations] = ipc_m[configurations].div(ipc_m[base], axis=0)
        ipc_m.dropna(axis=0, how="any", inplace=True)
        ipc_m.loc["gmean"] = ipc_m.apply(scipy.stats.gmean, axis=0).to_list()
        speedup_geomeans = ipc_m.loc["gmean"]
        ipc_ratio.dropna(axis=0, how="any", inplace=True)

        for i in indices_to_drop:
            speedup_geomeans.drop(configurations[i], axis=0, inplace=True)
            ipc_ratio.drop(configurations[i], axis=1, inplace=True, errors="ignore")

        container.append(pn.Row(f"""##{label}"""))
        percentages = []
        for index, value in speedup_geomeans.items():
            percentage = 100.0 * (value - 1.0)
            number = pn.indicators.Number(name=index, value=percentage, format="{value:.2f}%")
            percentages.append(number)
            print(f"{label} speedup for {number.name} vs. {base}: {number.value:.2f}%")

        container.append(pn.Row(*percentages))
        container.append("\n\n")

        # Define the different data you need
        # name : data
        groupings = {
            f"{label} Speedup": (ipc_ratio),
            f"{label} Speedup Self Sorted": (panthera.report.helpers.self_sort(ipc_ratio)),
        }

        # For each groupings make one plot
        for name, data in groupings.items():
            chart = create_trend_chart(data, title=f"{name}", ylabel=f"% of {label}", width=page_width)
            row = pn.Row()
            row.append(chart)
            container.append(row)
        container.append(
            pn.layout.Spacer(width=page_width, height=10, styles={"background": "black"}, margin=(50, 0, 40, 0))
        )


def overlay_hook(plot, element):
    # Adds right y-axis
    p = plot.handles["plot"]
    p.extra_y_scales = {"right": LinearScale()}
    p.extra_y_ranges = {"right": Range1d(min_ipc, max_ipc)}
    p.add_layout(LinearAxis(y_range_name="right"), "right")

    # find the last line and set it to right
    lines = [p for p in p.renderers if isinstance(p, GlyphRenderer)]
    lines[-1].y_range_name = "right"


def create_trend_plus_bar_chart(
    df, container, title="", xlabel=None, ylabel_line=None, ylabel_bar=None, log_line=True, width=page_width
):
    plot_bar = df.hvplot.scatter(x=xlabel, y=ylabel_bar, yaxis="left")
    plot_rate = df.hvplot.line(x=xlabel, y=ylabel_line, yaxis="right")
    global min_ipc
    global max_ipc
    max_ipc = df[ylabel_line].max()
    min_ipc = df[ylabel_line].min()
    plot_result = plot_bar.opts(logy=True, xlabel=xlabel) * plot_rate.opts(color="k")
    plot_result.opts(opts.Overlay(title=title, shared_axes=False))
    plot_result.opts(hooks=[overlay_hook], width=900)
    container.append(plot_result)


def speedup_bar_chart(df, metrics, selected_configs, container):
    idx = panthera.report.helpers.infer_index(df)
    base = configurations[base_index]
    pivot_df = df.pivot_table(values=metrics.keys(), index=[idx], columns=[CONFIGURATION])
    for metric, label in metrics.items():
        metric_speedup = speedup(df, metric, base_index)
        container.append(
            pn.pane.Markdown(f"""#{label} bars rolled up by {idx}
            """)
        )

        for config in selected_configs:
            pivot_cfg = pivot_df[(f"{metric}", f"{config}")].to_frame().copy()

            # for the rare case of having a '+' character in config names
            new_config = config.replace("+", "_")

            pivot_cfg.rename(columns={metric: label}, inplace=True)
            pivot_cfg.rename(columns={config: new_config}, inplace=True)

            # flatten the MultiIndex column of pivot_cfg
            pivot_cfg.columns = ["_".join(col).strip() for col in pivot_cfg.columns.values]
            cfg_data = pd.concat([pivot_cfg, metric_speedup[config]], axis=1)
            cfg_data.sort_values(by=config, inplace=True)
            cfg_data.rename(columns={config: f"{label}_speedup_{new_config}"}, inplace=True)

            y_column_bar = f"{label}_" + new_config
            y_column_line = f"{label}_speedup_{new_config}"
            create_trend_plus_bar_chart(
                cfg_data,
                container,
                title=f"{config} {label}",
                xlabel=idx,
                ylabel_line=y_column_line,
                ylabel_bar=y_column_bar,
                width=page_width,
            )


def df_to_xl(pivot_df, outputfile, idx):
    excel_filename = make_filepath(outputfile + ".xlsx", "pivot_table_", idx)
    print(f"writing Excel file {excel_filename}")
    excel_writer = pd.ExcelWriter(excel_filename, engine="xlsxwriter")
    pivot_df.to_excel(excel_writer, sheet_name=f"pivoted")
    excel_writer.close()

    # read modify and write the xl file again for some minor tuning
    wb_obj = openpyxl.load_workbook(excel_filename)
    sheet_obj = wb_obj["pivoted"]

    # remove one empty row
    sheet_obj.delete_rows(3, 1)

    # add filters to second row
    sc, sr, ec, er = openpyxl.utils.range_boundaries(sheet_obj.dimensions)
    start_cell = openpyxl.utils.get_column_letter(sc) + str(sr + 1)
    end_cell = openpyxl.utils.get_column_letter(ec) + str(sr + 1)
    sheet_obj.auto_filter.ref = f"{start_cell}:{end_cell}"

    # resize first column
    sheet_obj.column_dimensions["A"].width = 25
    wb_obj.save(excel_filename)


def create_argument_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-i", "--input", help="rollup CSV file", required=True)
    parser.add_argument("-o", "--output", help="name for output file")
    parser.add_argument(
        "-c", "--interactive", action="store_true", help="interactive mode, choose base and sorting config mode"
    )
    parser.add_argument(
        "-r", "--remove", action="store_true", help="remove sys.soc_0.core_0 from the column titles in pivot table"
    )
    parser.add_argument("--include", action="append", help="metrics to pivot", default=["sys.soc_0.core_0.ipc"])
    parser.add_argument("--include-file", help="file containing one metric per line to pivot")
    return parser


def main():
    parser = create_argument_parser()
    args = parser.parse_args()

    inputfile = args.input
    outputfile = args.output if args.output else ""
    pivot_values = args.include.copy()
    if args.include_file:
        with open(args.include_file) as include_file:
            for line in include_file:
                pivot_values.append(line.strip())

    # Import data into a pandas dataframe
    # input file is a rollup.csv file
    df = pd.read_csv(inputfile)

    pivot_metrics = {}
    for metric in pivot_values:
        if metric not in df.columns:
            print(f"{metric} non-existent in data file\n")
            continue
        metric_label = metric[metric.find("sys.soc_0.core_0.") + 17 :]
        pivot_metrics[f"{metric}"] = f"{metric_label}"

    if len(pivot_metrics) == 0:
        raise Exception("none of the given metrics exist in input data file")

    # Guess if the rollup provided is by trace, workload or suite
    idx = panthera.report.helpers.infer_index(df)

    # Create configurations list and error out if no base config present
    global configurations
    configurations = df[CONFIGURATION].unique().tolist()
    global base_index
    base_index = 0
    if args.interactive:
        for i, config in enumerate(configurations):
            print(f"{i}: {config}")
        base_index = int(input("Enter index of configuration you want to use as base: "))

    base = configurations[base_index]

    input_raw_name = inputfile.replace(" ", "_")
    input_raw_name = input_raw_name.replace(f".by_{idx}.csv", "")

    if outputfile == "":
        outputfile = f"{input_raw_name}"

    if len(configurations) > 1:
        ######################################################################
        #                                                                    #
        #                               Tab 1                                #
        #                                                                    #
        #                    Metric Speedup for all configs                  #
        #                                                                    #
        #             Self sorted + sorted for config of choice              #
        #                                                                    #
        ######################################################################

        container1 = pn.Column()
        container1.append(
            pn.pane.Markdown(f"""#Speedups for different configurations vs. {base}
                """)
        )

        global sort_idx
        sort_idx = -1

        if args.interactive:
            sort_idx = int(input("Enter index of configuration you want to use for sorting: "))

        global selected_configs
        selected_configs = configurations.copy()

        ipc_geomean_and_chart(df, pivot_metrics, container1)

        ######################################################################
        #                                                                    #
        #                               Tab 2                                #
        #                                                                    #
        #        Raw metric bar charts + speedup s curve for all configs     #
        #                                                                    #
        #                                                                    #
        ######################################################################

        container2 = pn.Column()

        # Append all the plots in an HTML file
        speedup_bar_chart(df, pivot_metrics, selected_configs, container2)

        top = pn.Tabs()
        top.append(("S-curve", container1))
        top.append(("Bar Charts", container2))

        html_filename = make_filepath(outputfile + ".html", "speedup_plots_", idx)
        print(f"Generating HTML report file {html_filename}")
        top.save(html_filename, title="Speedup charts", resources=bokeh.resources.INLINE)

    ######################################################################
    #                                                                    #
    #                       Pivot Table Excel File                       #
    #                                                                    #
    #                outputs an excel file with one tab                  #
    #                                                                    #
    #                      tab1: pivoted dataframe                       #
    #                                                                    #
    ######################################################################

    pivot_df = df.pivot_table(values=pivot_values, index=[idx], columns=[CONFIGURATION])

    pivot_df.columns.names = ["", f"{idx}"]
    if args.remove:
        pivot_df.rename(columns=pivot_metrics, inplace=True)
    df_to_xl(pivot_df, outputfile, idx)


if __name__ == "__main__":
    main()
else:
    print("this report has to be called as a standalone tool.\n panthera report.reports.pivot <args>")
