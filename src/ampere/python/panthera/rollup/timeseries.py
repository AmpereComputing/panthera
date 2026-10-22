#! /usr/bin/env python3
# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


import logging
import os
import re

import pandas as pd

from panthera.cheetah.job import Job

from .metric_formula import MetricFormula, compute_all_formulas


class MetricData:
    def __init__(self, csv, name=None, job=None, additional_metrics=None, output_spec=None, trace_name_format=None):
        if not os.path.isfile(csv):
            raise FileNotFoundError(csv)

        self.filename = csv
        self._name = name
        self.job = job
        self._additional_metrics = additional_metrics

        # Assume cheetah-like study and consistent naming by default
        output_spec = output_spec or r"/{configuration}/{workload}.{trace}"
        self._output_spec = self._format_capture(output_spec)

        trace_name_format = trace_name_format or r"{workload}.{trace}"
        self._trace_name_format = trace_name_format

    @classmethod
    def _format_capture(cls, output_spec):
        spec_regex = re.sub(r"\{(\w+?)\}", r"(?P<\1>__wordspec__)", output_spec)
        # limited escape
        spec_regex = spec_regex.replace(".", r"\.").replace("-", r"\-")
        try:
            return re.compile(spec_regex.replace("__wordspec__", r"[\w\d_\-]+?") + r"\.[^/]+$")
        except re.error:
            raise ValueError(f"Invalid output specification: {output_spec}")

    @classmethod
    def _expand_format(cls, format_str, values):
        for key, value in values.items():
            format_str = format_str.replace("{" + key + "}", value)

        return format_str

    @property
    def _extracted_meta(self):
        path = os.path.realpath(self.filename)
        capture = self._output_spec.search(path)
        if not capture:
            raise ValueError(f"Unable to extract trace meta from file-path: {path}")

        return dict(capture.groupdict())

    @property
    def name(self):
        """Trace name."""
        if self._name:
            return self._name

        trace_name = self._expand_format(self._trace_name_format, self._extracted_meta)
        return re.sub(r"(\.(summary|ts))?\.csv(\.gz)?$", "", trace_name)

    @property
    def meta(self):
        if self.job:
            return self.job.meta

        meta = self._extracted_meta
        meta.pop("trace")
        meta["weight"] = 1
        return meta

    @property
    def df(self):
        df = self.read_csv()
        if not self._additional_metrics:
            return df

        return pd.concat(
            [
                df,
                pd.DataFrame(
                    {metric: [0] * (df.shape[0] - 1) + [value] for metric, value in self._additional_metrics.items()}
                ),
            ]
        )

    def read_csv(self):
        logging.debug("reading {}".format(self.filename))
        try:
            df = pd.read_csv(self.filename)
        except Exception as e:
            raise Exception(f"Issue reading {self.filename}") from e
        return df.set_index(df["stop_time"])

    def compute_formulas(self, formulas, keep_factored=True, emit_warnings=True):
        metric = MetricFormula(self.df, emit_warnings=emit_warnings)
        formulas.compute_all(metric, emit_warnings=emit_warnings)
        if keep_factored:
            return metric.df

        noslash = ["/" not in name for name in metric.df.columns.values]
        return metric.df.loc[:, noslash]


class Summary(MetricData):
    @classmethod
    def from_job(cls, job, suffix=None, **kwargs):
        path = job.summary_abspath
        if suffix is not None:
            path = path.replace(Job.FILE_SUFFIXES["summary"], suffix)
        return cls(csv=job.summary_abspath, name=job.name, job=job, **kwargs)


class TimeSeries(MetricData):
    @classmethod
    def from_job(cls, job, suffix=None, **kwargs):
        path = job.timeseries_abspath
        if suffix is not None:
            path = path.replace(Job.FILE_SUFFIXES["timeseries"], suffix)
        return cls(csv=job.timeseries_abspath, name=job.name, job=job, **kwargs)
