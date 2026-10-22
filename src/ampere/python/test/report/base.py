# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import json
import os
import tempfile
import unittest
from itertools import product

import pandas as pd
from panthera.report.helpers import ReportGenerator

PRODUCTS = [
    "Siryn",
    "Banshee",
    "Rogue",
    "Domino",
    "Nebula",
]


def create_rollup_data(
    output: str,
    configs: list | None = None,
    traces: list | None = None,
    workloads: list | None = None,
    suites: list | None = None,
    metrics: list | None = None,
    dates: list | None = None,
    **kwargs,
):
    configs = configs or PRODUCTS
    if dates:
        configs = [f"{date}.{config}" for date in dates for config in configs]
    metrics = metrics or [
        "sys.soc_0.core_0.ipc",
    ]
    explicit = {
        "configuration": configs,
    }
    if not dates or traces:
        traces = traces or [f"trace_{i}" for i in range(5)]
        explicit["trace"] = traces
    if workloads:
        explicit["workload"] = workloads
    if suites:
        explicit["suite"] = suites

    index = pd.MultiIndex.from_product(
        list(explicit.values()),
        names=list(explicit.keys()),
    )
    column_data = {column: [1] * index.shape[0] for column in metrics}
    data = pd.DataFrame(data=column_data, index=index).reset_index()

    if not suites:
        data["suite"] = "suite"
        if not workloads:
            if not dates or traces:
                data["workload"] = data["trace"].apply(
                    lambda x: "-".join(x.split("-")[:-1]) if "-" in x else "workload"
                )
            else:
                data["workload"] = "workload"

    data.loc[data["configuration"].str.endswith("-gcc10"), "suite"] = "ISPEC17"

    data.to_csv(output, index=False)
    return output


def create_study_json(
    output: str,
    configs: list | None = None,
    traces: list | None = None,
    **kwargs,
):
    configs = configs or PRODUCTS
    traces = traces or [f"trace_{i}" for i in range(5)]
    jobs = [
        {
            "name": "_".join(job),
            "directory": job[0],
            "cmds": ["sim", job[1]],
            "meta": {"configuration": job[0], "suite": "suite", "workload": "workload", "trace": job[1]},
        }
        for job in product(configs, traces)
    ]
    study = {
        "name": "test_study",
        "study_creator": "cheater",
        "study_directory": os.path.dirname(output),
        "cmd_pool": jobs,
    }
    with open(output, "w") as f:
        json.dump(study, f)

    return output


class ReportTest(unittest.TestCase):
    @classmethod
    def rollup_args(cls):
        return {}

    @classmethod
    def setUpClass(cls):
        cls.working_dir = tempfile.TemporaryDirectory()
        cls.rollup_csv = create_rollup_data(os.path.join(cls.working_dir.name, "rollup.trace.csv"), **cls.rollup_args())
        cls.study_json = create_study_json(os.path.join(cls.working_dir.name, "study.json"), **cls.rollup_args())

    @classmethod
    def tearDownClass(cls):
        cls.working_dir.cleanup()

    def run_report(self, path):
        rg = ReportGenerator(self.rollup_csv, output_dir=self.working_dir.name, reports=[path])
        rg.execute_report(rg.reports[0], {"format": "png"})

    def check_output(self, name, shape=None, columns=None):
        output_csv = pd.read_csv(os.path.join(self.working_dir.name, name))
        if shape:
            self.assertEqual(output_csv.shape, shape)
        if columns:
            self.assertEqual(output_csv.columns.tolist(), list(columns))
