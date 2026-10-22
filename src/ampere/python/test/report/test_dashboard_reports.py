# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os

from base import PRODUCTS, ReportTest
from panthera.report.__main__ import find_reports
from panthera.report.helpers import likely_standalone


class TestWorkloadReportsMeta(type):
    def __new__(cls, name, bases, dict):
        expectations = {
            "dashboard_spec_score": {
                "name": "SPEC2017INT_scores.workload.csv",
                "shape": (25, 15),
                "columns": (
                    "date",
                    "500-perlbench",
                    "502-gcc",
                    "505-mcf",
                    "520-omnetpp",
                    "523-xalancbmk",
                    "525-x264",
                    "531-deepsjeng",
                    "541-leela",
                    "548-exchange2",
                    "557-xz",
                    "Geomean",
                    "configuration",
                    "product",
                    "rate",
                ),
            },
            "dashboard_workload_ipc": {
                "name": "study_list_mean_ipc.workload.csv",
                "shape": (50, 3),
                "columns": ("date", "configuration", "sys.soc_0.core_0.ipc"),
            },
        }

        def generate_test(path):
            def test(self):
                self.run_report(path)
                if checks := expectations.get(os.path.splitext(os.path.basename(path))[0]):
                    self.check_output(**checks)

            return test

        reports = find_reports()
        for report_name, path in reports.items():
            if not likely_standalone(path) and "dashboard" in report_name and "suite" not in report_name:
                dict[f"test_{report_name}"] = generate_test(path)

        return type.__new__(cls, name, bases, dict)


class TestWorkloadReports(ReportTest, metaclass=TestWorkloadReportsMeta):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()

    @classmethod
    def tearDownClass(cls):
        super().tearDownClass()

    @classmethod
    def rollup_args(cls):
        return {
            "configs": PRODUCTS + [f"{product}-Rate-N-gcc10" for product in PRODUCTS],
            "workloads": [
                "500-perlbench-1",
                "500-perlbench-2",
                "500-perlbench-3",
                "502-gcc-1",
                "502-gcc-2",
                "502-gcc-3",
                "502-gcc-4",
                "502-gcc-5",
                "505-mcf-1",
                "520-omnetpp-1",
                "523-xalancbmk-1",
                "525-x264-1",
                "525-x264-2",
                "525-x264-3",
                "531-deepsjeng-1",
                "541-leela-1",
                "548-exchange2-1",
                "557-xz-1",
                "557-xz-2",
                "557-xz-3",
            ],
            "metrics": [
                "sys.soc_0.core_0.ipc",
                "sys.soc_0.core_0.gpc.cycle",
                "sys.soc_0.core_0.gpc.instruction_commit",
            ],
            "dates": [
                "2025-10-09",
                "2025-10-10",
                "2025-10-11",
                "2025-10-12",
                "2025-10-13",
            ],
        }


class TestSuiteReportsMeta(type):
    def __new__(cls, name, bases, dict):
        expectations = {
            "dashboard_suite_ipc": {
                "name": "study_list_ipc.suite.csv",
                "shape": (75, 4),
                "columns": ("date", "configuration", "suite", "sys.soc_0.core_0.ipc"),
            },
        }

        def generate_test(path):
            def test(self):
                self.run_report(path)
                if checks := expectations.get(os.path.splitext(os.path.basename(path))[0]):
                    self.check_output(**checks)

            return test

        reports = find_reports()
        for report_name, path in reports.items():
            if not likely_standalone(path) and "dashboard_suite" in report_name:
                dict[f"test_{report_name}"] = generate_test(path)

        return type.__new__(cls, name, bases, dict)


class TestSuiteReports(ReportTest, metaclass=TestSuiteReportsMeta):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()

    @classmethod
    def tearDownClass(cls):
        super().tearDownClass()

    @classmethod
    def rollup_args(cls):
        return {
            "configs": PRODUCTS,
            "suites": [
                "Media",
                "Science",
                "Web",
            ],
            "metrics": [
                "sys.soc_0.core_0.ipc",
            ],
            "dates": [
                "2025-10-09",
                "2025-10-10",
                "2025-10-11",
                "2025-10-12",
                "2025-10-13",
            ],
        }
