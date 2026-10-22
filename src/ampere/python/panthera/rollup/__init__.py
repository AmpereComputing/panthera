# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Rollup weighted metrics from summary/timeseries."""

import pandas as pd
import scipy.stats

from .metric_formula import FormulaCache, MetricFormula
from .timeseries import MetricData, Summary, TimeSeries

CONFIGURATION = "configuration"
TRACE = "trace"
WORKLOAD = "workload"
SUITE = "suite"
COMPONENT = "component"

WEIGHT = "weight"
GROUPBY = (TRACE, WORKLOAD, SUITE)

__tools__ = [
    "cat",
    "speedup",
    "viewer",
]
