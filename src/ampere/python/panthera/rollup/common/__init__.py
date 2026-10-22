# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import re as _re


def pki(metric, core_metrics):
    """Per-kilo instruction."""
    return 1000 * metric / core_metrics.pmu.inst_retired


def per_cycle(metric, core_metrics):
    """Per cycle."""
    return metric / core_metrics.pmu.cpu_cycles


def pkc(metric, core_metrics):
    """Per-kilo cycle."""
    return 1000 * per_cycle(metric, core_metrics)


def nanoseconds(metrics):
    return 1e9 * metrics.seconds


def sum_by_regex(metrics, regex, weight_by_factor=False):
    """Sum all metrics matching regex."""
    subset = metrics.df.filter(regex=regex)
    if weight_by_factor:
        weights = [int(_re.sub(r"^.*?:", "", c)) for c in subset.columns.to_list()]
        subset = subset * weights

    return subset.sum(axis=1)


from . import cache, l1_prefetcher, l2_prefetcher
