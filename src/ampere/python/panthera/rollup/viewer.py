# Copyright (c) 2020-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
View a rollup of cheetah simulation data in an interactive web based tool.
"""

import sys

import bokeh.server.server
import holoviews as hv
import pandas as pd

from panthera import ArgumentParser, setup_logging
from panthera.rollup import CONFIGURATION, SUITE, TRACE, WORKLOAD

hv.extension("bokeh")

SUITES = [
    "All",
    "ISPEC",
    "FSPEC",
    "DaCapo",
    "Renaissance",
    "CloudInfra",
    "Graphs",
    "MLPack",
    "Media",
    "SpecJBB",
    "CAD",
    "Crypto",
    "Micro",
]

DEFAULT_METRICS = ["sys.soc_0.core_0.ipc"]


class Viewer:
    def __init__(self, filenames, metrics, chart_type, height, width):
        self.filenames = filenames
        self.metrics = metrics
        self.chart_type = chart_type
        self.height = height
        self.width = width
        self.dfs = {name: pd.read_csv(name) for name in self.filenames}

    def callback(self, filename, metric, suite):
        if self.chart_type == "Scatter":
            return self.scatter(filename, metric, suite)
        elif self.chart_type == "Bar":
            return self.bar(filename, metric, suite)

    def scatter(self, filename, metric, suite):
        df = self.dfs[filename]
        if suite != "All":
            df = df[df["suite"] == suite]
        idx_column = self.index_column(df)
        scatter = hv.Scatter(df, idx_column, [metric, CONFIGURATION])
        scatter = scatter.sort(metric)
        scatter.opts(
            height=self.height,
            width=self.width,
            color=CONFIGURATION,
            size=4,
            xrotation=90,
            tools=["hover"],
            cmap="Colorblind",
        )
        return scatter

    def bar(self, filename, metric, suite):
        df = self.dfs[filename]
        if suite != "All":
            df = df[df["suite"] == suite]
        idx_column = self.index_column(df)
        bar = hv.Bars(df, [idx_column, CONFIGURATION], metric)
        bar.opts(height=self.height, width=self.width, xrotation=90, tools=["hover"], cmap="Colorblind")
        return bar

    def index_column(self, df):
        if TRACE in df:
            return TRACE
        elif WORKLOAD in df:
            return WORKLOAD
        elif SUITE in df:
            return SUITE
        else:
            return 0

    def render(self):
        def cb(filename, metric, suite):
            return self.callback(filename, metric, suite)

        kdims = ["File", "Metric", "Suite"]
        values = {"Metric": self.metrics, "File": self.filenames, "Suite": SUITES}
        hmap = hv.DynamicMap(cb, kdims=kdims).redim.values(**values)
        return hv.renderer("bokeh").app(hmap, show=True, new_window=True)


def parse_args():
    parser = ArgumentParser(description=__doc__, tool="rollup.viewer")

    parser.add_argument("inputs", help="input CSV files", nargs="+")
    parser.add_argument("-c", "--chart", help="chart type", choices=["Scatter", "Bar"], default="Scatter")
    parser.add_argument("--height", help="chart height (pixels)", type=int, default=600)
    parser.add_argument("--width", help="chart width (pixels)", type=int, default=1000)
    parser.add_argument("-m", "--metric", help="metric to view", action="append", default=DEFAULT_METRICS)
    parser.add_argument("--debug", help="Enable debug logging", action="store_true")

    argv = sys.argv[1:] + ["@base.cfg"]
    return parser.parse_args(args=argv)


def main(args):
    setup_logging(debug=args.debug)

    viewer = Viewer(args.inputs, args.metric, args.chart, args.height, args.width)
    app = viewer.render()

    server = bokeh.server.server.Server({"/": app}, port=0)
    server.stop()


if __name__ == "__main__":
    main(parse_args())
