# Copyright (c) 2024-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import subprocess
import unittest
from glob import glob
from os.path import dirname, join
from tempfile import TemporaryDirectory

from panthera import install_dir, setup_logging
from panthera.constants import SUMMARY_CSV_SUFFIX, TIMESERIES_CSV_SUFFIX

panthera_py = os.path.realpath(join(install_dir, "bin", "panthera"))
FIXTURES_DIR = join(dirname(dirname(__file__)), "fixtures")
REGR_DIR = join(FIXTURES_DIR, "studies", "regr")


class TestRollupRegr(unittest.TestCase):
    @classmethod
    def run_rollup_regr(cls, *args):
        cmd = [panthera_py, "snippets.rollup_regr"] + list(args)
        cmd += [
            "--num-threads", "1",
            "--debug",
        ]  # fmt: skip
        return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True)

    def run_basic_rollup_regr(self, *args):
        return self.run_rollup_regr(
            self.output_dir,
            REGR_DIR,
            join(REGR_DIR, "snippets.json"),
            "--ts_metric", "core.gpc.instruction_commit",
            "--ts_interval", "10",
            "--ts_start", "25",
            "--ts_stop", "75",
            "--evp-config", "all.cfg",
            "--rollup-formula", "basic_formula.py",
            *args,
        )  # fmt: skip

    def verify_files(self, directory, expected):
        output_files = os.listdir(directory)
        self.assertEqual(set(output_files), expected)
        for output_file in output_files:
            self.assertGreater(os.path.getsize(join(directory, output_file)), 0)

    def verify_snippet_files(self, directory, snippets, extra=None):
        files = [
            "log",
            "run.log",
            SUMMARY_CSV_SUFFIX,
            TIMESERIES_CSV_SUFFIX,
        ] + (extra or [])
        for name in snippets:
            for file in files:
                path = join(directory, f"{name}.{file}")
                self.assertTrue(os.path.isfile(path))
                self.assertGreater(os.path.getsize(path), 0)

    def setUp(self):
        setup_logging(True)
        self.working_dir = TemporaryDirectory()
        self.output_dir = join(self.working_dir.name, "output")
        self.local_output_dir = join(self.output_dir, "regr")

    def tearDown(self):
        self.working_dir.cleanup()
        setup_logging(False)

    def test_help(self):
        self.run_rollup_regr("--help")

    def test_rollup(self):
        output = self.run_basic_rollup_regr().stdout.decode()
        self.assertRegex(output, r"Processing .*?something_0.*? to .*?something_0.*? \(25, 75\)")
        self.assertRegex(output, r"Processing .*?something_2.*? to .*?something_2.*? \(5, 25\)")

        self.verify_files(
            self.output_dir,
            {"regr", "rollup.by_trace.csv", "rollup.by_workload.csv", "study.json"},
        )
        self.verify_snippet_files(self.local_output_dir, {"something_0", "something_1", "something_2"})

    def test_rollup_all(self):
        self.run_basic_rollup_regr("--summarize-all")
        self.verify_files(
            self.output_dir,
            {
                "regr",
                "rollup.all.by_trace.csv",
                "rollup.all.by_workload.csv",
                "rollup.by_trace.csv",
                "rollup.by_workload.csv",
                "study.json",
            },
        )
        self.verify_snippet_files(
            self.local_output_dir,
            {"something_0", "something_1", "something_2"},
            extra=[
                f"all.{SUMMARY_CSV_SUFFIX}",
                f"all.{TIMESERIES_CSV_SUFFIX}",
            ],
        )

    def test_rollup_truncate_log(self):
        self.run_basic_rollup_regr("--truncate-run-log", "10")

        for log in glob(join(self.local_output_dir, "*.run.log")):
            with open(log, "r") as f:
                self.assertEqual(len(f.readlines()), 25)
