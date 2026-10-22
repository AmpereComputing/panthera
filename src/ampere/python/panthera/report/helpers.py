# Copyright (c) 2019-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
This module contains helper functions that can be called without decoration in report scripts.
"""

import argparse
import ast
import logging
import os.path
import re

import matplotlib
import numpy as np
import pandas as pd
import scipy.stats

matplotlib.use("svg", force=True)
from typing import Any

import matplotlib.pyplot as plt

from panthera import ArgumentParser
from panthera.rollup import COMPONENT, CONFIGURATION, SUITE, TRACE, WORKLOAD

pd.options.display.float_format = "{:,.3f}".format

input_file = None
output_dir = "."
rollup_index = None


def get_report_info(filepath: str) -> str:
    with open(filepath, "r") as f:
        tree = ast.parse(f.read())
        standalone = any(
            isinstance(node, ast.FunctionDef) and node.name in {"main", "parse_args"}
            for node in ast.iter_child_nodes(tree)
        )
        return standalone, ast.get_docstring(tree)


def likely_standalone(filepath: str) -> bool:
    with open(filepath, "r") as f:
        tree = ast.parse(f.read())
        try:
            return tree.body[-1].test.left.id == "__name__"
        except:
            return False


def create_argument_parser(
    reports: dict[str, str] | None = None,
    tool: str | None = None,
    description: str | None = None,
) -> ArgumentParser:
    params = {}
    if reports:
        epilog = ["List of reports that can be generated:"]
        for name, path in reports.items():
            standalone, description = get_report_info(path)
            if standalone:
                continue
            if description:
                description = description.splitlines()[0]
            else:
                description = ""
            epilog.append("   {:<25}{}".format(name, description))
        params["epilog"] = "\n".join(epilog)
    if tool:
        params["tool"] = f"report.{tool}"

    parser = ArgumentParser(
        description=description or __doc__, formatter_class=argparse.RawDescriptionHelpFormatter, **params
    )
    parser.add_argument("input", nargs="+", help="rollup CSV files")
    parser.add_argument("--output", help="directory for output files")
    parser.add_argument(
        "--format",
        help="image format for charts",
        choices=["svg", "png"],
        default="svg",
    )
    if reports:
        parser.add_argument(
            "--report",
            required=True,
            action="append",
            choices=reports,
            help="report script to run",
        )

        parser.add_argument("--base", required=False, help="baseline configuration for plots")
        parser.add_argument(
            "--mean",
            default="arithmetic",
            choices=["geometric", "arithmetic"],
            help="which type of mean to use for aggregation",
        )

    return parser


def execute_report_script(
    script: str,
    df: pd.DataFrame,
    ifile: str,
    odir: str,
    args: dict[str, Any],
    parsed_args,
    clist: list[str] | None,
):
    """Execute a report script in the context of report_helper module.
    :param script: code object to exec
    :param df: dataframe
    :param odir: output directory name
    """
    global input_file
    input_file = ifile
    global output_dir
    output_dir = odir
    global rollup_index
    rollup_index = infer_index(df)
    global file_ext
    file_ext = args["format"]
    output = None
    global all_args
    all_args = parsed_args
    global config_list
    config_list = clist
    glbs = globals()
    glbs["df"] = df
    exec(script, None, glbs)
    return output


class ReportGenerator:
    def __init__(
        self,
        filename: str,
        output_dir: str,
        parsed_args=None,
        reports: list[str] | None = None,
    ):
        reports = reports or []
        self.filename = filename
        self.df = pd.read_csv(filename)
        self.output_dir = output_dir
        self.reports = [self.compile_report(s) for s in reports]
        self.args = parsed_args
        self.config_list = None

        if parsed_args is not None and parsed_args.base and "configuration" in self.df:
            self.config_list = list(dict.fromkeys(self.df["configuration"]))
            if parsed_args.base in self.config_list:
                self.config_list.remove(parsed_args.base)
                self.config_list.insert(0, parsed_args.base)
            else:
                print(
                    f"--base {parsed_args.base} was not found in the list of configurations: {' '.join(self.config_list)}"
                )
                exit()

    @classmethod
    def from_args(cls, filename: str, args, all_reports: dict[str, str] | None = None) -> "ReportGenerator":
        input_dir = os.path.dirname(filename)
        output_dir = args.output if args.output else input_dir
        if not all_reports:
            return cls(filename, output_dir, args)

        return cls(filename, output_dir, args, reports=[all_reports[i] for i in args.report])

    @staticmethod
    def compile_report(report_filename: str):
        logging.info("compiling {}".format(report_filename))
        with open(report_filename) as f:
            return compile(f.read(), report_filename, "exec")

    def execute_report(self, report: str, report_args: dict[str, Any]):
        return execute_report_script(
            report, self.df, self.filename, self.output_dir, report_args, self.args, self.config_list
        )


def infer_index(df: pd.DataFrame) -> str:
    """Infer the index based on what columns are present"""

    if TRACE in df:
        return TRACE
    elif WORKLOAD in df:
        return WORKLOAD
    else:
        return SUITE


def rollup(df: pd.DataFrame, metrics: list[str] | re.Pattern) -> pd.DataFrame:
    """Rollup the specified metric by trace/workload/suite and configuration.
    :param df: dataframe
    :param metrics: metric to rollup
    :return: dataframe
    """
    dimensions = []
    idx = infer_index(df)
    if idx == TRACE:
        dimensions.append(WORKLOAD)
        dimensions.append(SUITE)
    elif idx == WORKLOAD:
        dimensions.append(SUITE)
    dimensions.append(idx)
    dimensions.append(CONFIGURATION)

    if isinstance(metrics, re.Pattern):
        metrics = list(c for c in df.columns if metrics.match(c))

    ndf = df.reindex(dimensions + metrics, axis=1)
    grouped = ndf.groupby(dimensions)
    return grouped.mean()


def speedup(df: pd.DataFrame, metric: str) -> pd.DataFrame:
    """Rollup the speedup of 'metric' for all of the configurations.
    The speedup is the ratio of the metric value for a configuration divided by the value in the base configuration
    which is assumed to be the first configuration. The function subtracts 1 from the ratio so that positive values
    indicate a speedup and negative values indicate a slowdown.
    :param df: dataframe
    :param metric: metric to rollup
    :return: dataframe
    """
    idx = infer_index(df)
    configurations = list(dict.fromkeys(df[CONFIGURATION]))
    try:
        if config_list and (filtered_configs := [c for c in config_list if c in configurations]):
            configurations = filtered_configs
    except NameError:
        # config_list is set when called at the command line, not by tests
        pass

    base = configurations[0]
    final = configurations[-1]
    ndf = df.reindex([idx, CONFIGURATION, metric], axis="columns")
    metric_table = ndf.pivot(index=idx, columns=CONFIGURATION, values=metric)
    metric_table = metric_table.reindex(configurations, axis="columns")
    su = metric_table.div(metric_table[base], axis=0)
    return su.sort_values(by=final)


def self_sort(df: pd.DataFrame, ascending: bool = True) -> pd.DataFrame:
    """
    Self-sort columns in a dataframe, without attempting to keep values in
    their proper rows. This (intentionally) drops the index(es), meaning
    trace/workload/suite names will be lost. Returns the self-sorted dataframe.
    """
    if ascending:
        return pd.DataFrame(np.sort(df.values, axis=0), columns=df.columns)
    else:
        arr = df.values
        arr.sort(axis=0)
        arr = arr[::-1]
        return pd.DataFrame(arr, columns=df.columns)


def make_filepath(filename: str) -> str:
    """Make a filepath based on the output directory, rollup grouping and filename
    :param filename: basename of the file to be created
    :return: full path of file to be created
    """
    (basename, ext) = os.path.splitext(filename)
    return os.path.join(output_dir, basename + f".{rollup_index}" + ext)


def create_plot(df: pd.DataFrame, filename: str, **kwargs) -> None:
    """Create a plot of the dataframe and write it as an SVG to filename.
    :param df: dataframe
    :param filename: output file name, will be created in the output directory
    :param kwargs: passed to the pandas plot method
    :return:
    """
    filepath = make_filepath(filename)
    if "quiet" in kwargs:
        del kwargs["quiet"]
    else:
        logging.info("creating plot {}".format(filepath))

    flag = False
    if "mean" in kwargs:
        flag = True
        mean = kwargs["mean"]
        del kwargs["mean"]

        std = kwargs["std"]
        del kwargs["std"]

        var = kwargs["var"]
        del kwargs["var"]

    if flag:
        textstr = "\n".join((r"$\mu=%.2f$" % (mean,), r"$\sigma=%.2f$" % (std,), r"$\sigma^2=%.2f$" % (var,)))

    if "y_label" in kwargs:
        y_label = kwargs["y_label"]
        del kwargs["y_label"]
    else:
        y_label = None

    if "secondary_y_label" in kwargs:
        secondary_y_label = kwargs["secondary_y_label"]
        del kwargs["secondary_y_label"]
    else:
        secondary_y_label = None

    if "stacked" in kwargs:
        kind = "bar"
    elif "scatter" in kwargs:
        kind = "scatter"
        del kwargs["scatter"]
    else:
        if df.index.size < 50:
            kind = "bar"
        else:
            kind = "line"
            kwargs["rot"] = 45

    plot_args = {"kind": kind, "figsize": (15, 9)}
    cmap = matplotlib.colormaps["tab10"]
    if kind != "scatter":
        plot_args["cmap"] = cmap
    plot_args.update(kwargs)

    ax = df.plot(**plot_args)
    ax.grid(True, which="major", axis="y")
    if y_label:
        ax.set_ylabel(y_label)
    if secondary_y_label:
        ax.right_ax.set_ylabel(secondary_y_label)

    if kind == "scatter":
        x = np.linspace(*ax.get_xlim())
        ax.plot(x, x)

    if flag:
        props = dict(boxstyle="round", facecolor="wheat", alpha=0.5)
        ax.text(0.95, 0.95, textstr, transform=ax.transAxes, fontsize=10, verticalalignment="top", bbox=props)

    plt.gcf().subplots_adjust(top=0.95, bottom=0.25, left=0.05, right=0.95)

    #    if 'stacked' in kwargs:
    #        plt.legend(labelspacing=-2.5, loc='top middle')

    plt.savefig(filepath)
    plt.close()


def stackup_plot(df: pd.DataFrame, filename: str, **kwargs) -> None:
    """Create a stacked bar chart of the dataframe and write it as an SVG to filename.
    The output file will be created in the output directory.
    :param df: dataframe to plot
    :param filename: name of output file
    :param kwargs: passed to the pandas plot method
    :return: None
    """
    plot_args = {"kind": "bar", "stacked": True, "figsize": (15, 9)}
    plot_args.update(kwargs)
    create_plot(df, filename, **plot_args)


def create_csv(df: pd.DataFrame, filename: str, quiet: bool = False) -> None:
    """Create a CSV file of the dataframe.
    The CSV file will be created in the output directory.
    :param df: dataframe
    :param filename: name of output file
    :param quiet: don't print messages if True
    :return: None
    """
    filepath = make_filepath(filename)
    if not quiet:
        logging.info("creating CSV {}".format(filepath))
    df.to_csv(filepath)


def metric_report(df: pd.DataFrame, metric: str) -> pd.DataFrame:
    base_name = metric.replace(".", "_")

    rup = rollup_and_pivot(df, metric)
    create_plot(rup, filename=f"{base_name}.{file_ext}", title=f"{metric}", quiet=True)

    if "sys.soc_0.core_0.ipc" in metric:
        create_csv(rup, filename=f"{base_name}.csv", quiet=True)

    sup = speedup(df, metric)
    create_plot(
        sup, filename=f"{base_name}_speedup.{file_ext}", title=f"{metric} speedup", y_label="Speedup", quiet=True
    )
    create_csv(sup, filename=f"{base_name}_speedup.csv", quiet=True)

    if all_args is not None and all_args.mean and all_args.mean == "geometric" and not "raw" in metric:
        from scipy.stats import gmean

        mean = rup.apply(gmean)
    else:
        mean = rup.mean()
    mean.name = metric
    return mean


def ipc_validity_warnings(df: pd.DataFrame) -> None:
    if "trace" in df.columns or "workload" not in df.columns:
        logging.warning(
            "IPC results are not valid starting from trace or suite rollup. Run from rollup.by_workload.csv instead"
        )
    if all_args is None or all_args.mean is None or all_args.mean != "geometric":
        logging.warning("IPC results are not valid without --mean geometric")


def correlation_report(df: pd.DataFrame, metric: str) -> pd.Series:
    base_name = metric.replace(".", "_")

    rup = rollup_and_pivot(df, metric)
    create_csv(rup, filename=f"{base_name}_rollup.csv", quiet=True)
    create_plot(
        rup,
        filename=f"{base_name}_correlation.{file_ext}",
        title=f"{metric}",
        scatter=True,
        x=rup.columns[0],
        y=rup.columns[1],
        quiet=True,
    )

    sup = speedup(df, metric)
    create_plot(
        sup, filename=f"{base_name}_speedup.{file_ext}", title=f"{metric} speedup", y_label="Speedup", quiet=True
    )
    create_csv(sup, filename=f"{base_name}_speedup.csv", quiet=True)

    mean = rup.mean()
    mean.name = metric
    return mean


def rollup_by_suite(df: pd.DataFrame, metrics: list[str]) -> pd.DataFrame:
    by_suite = df.groupby([CONFIGURATION, SUITE]).mean().reset_index()
    return by_suite.pivot(index=SUITE, columns=CONFIGURATION, values=metrics)


def rollup_and_pivot(df: pd.DataFrame, metric: str) -> pd.DataFrame:
    rup = rollup(df, [metric])
    idx = infer_index(df)
    data = rup.reset_index().pivot(index=idx, columns=CONFIGURATION, values=metric)
    try:
        if config_list and (configurations := [c for c in config_list if c in data.columns]):
            return data[configurations]
    except NameError:
        # config_list is set when called at the command line, not by tests
        pass

    return data


def read_data_file(filename: str) -> pd.DataFrame:
    """Create a DataFrame from a CSV file in the reports data directory"""
    script_dir = os.path.dirname(__file__)
    filepath = os.path.join(script_dir, "reports", "data", filename)
    return pd.read_csv(filepath)


def compute_spec_scores(df: pd.DataFrame, frequency: float, ref_file_name: str):
    return compute_spec_scores_universal(df, frequency, ref_file_name, 0)


def compute_spec_scores_subcomponent(df: pd.DataFrame, frequency: float, ref_file_name: str):
    return compute_spec_scores_universal(df, frequency, ref_file_name, 1)


def compute_spec_scores_universal(
    df: pd.DataFrame, frequency: float, ref_file_name: str, subcomponent: int
) -> pd.DataFrame:
    """Compute SPEC scores from simulation data.
    Compute the scores by projecting time for each benchmark, computing the ratio of the benchmark time
    compared to the SPEC reference system and computing the geometric of the benchmark scores.
    :param df: dataframe with the per trace cycles and instruction data
    :param frequency: frequency of simulation in Hz
    :return: dataframe with SPEC scores for each configuration
    """
    ref_data = read_data_file(ref_file_name)
    workloads = ref_data[WORKLOAD]

    cycles = rollup_and_pivot(df, "sys.soc_0.core_0.gpc.cycle")
    instructions = rollup_and_pivot(df, "sys.soc_0.core_0.gpc.instruction_commit")
    cpi = cycles / instructions

    instructions = ref_data["instructions"]
    instructions.index = workloads

    spec_cpi = cpi.loc[workloads]
    spec_cycles = spec_cpi.mul(instructions, axis=0)
    spec_time = spec_cycles / frequency

    ref_time = ref_data["time"]
    ref_time.index = workloads

    if subcomponent == 1:
        subcomponent_cpi = spec_cpi
        subcomponent_cycles = subcomponent_cpi.mul(instructions, axis=0)
        subcomponent_time = subcomponent_cycles / frequency

        subcomponent_df = ref_data.loc[:, [WORKLOAD, COMPONENT]]
        subcomponent_df = subcomponent_df.set_index(WORKLOAD)
        subcomponent_time.loc[:, COMPONENT] = subcomponent_df.loc[:, COMPONENT]

        spec_time = subcomponent_time.groupby(COMPONENT).sum()

        ref_time = ref_data[["time", "component"]].groupby(COMPONENT).max().time

    spec_scores = (1 / spec_time).mul(ref_time, axis=0)
    spec_result = scipy.stats.gmean(spec_scores)
    spec_scores.loc["Geomean"] = spec_result
    return spec_scores
