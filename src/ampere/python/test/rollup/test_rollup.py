# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import glob
import os
import unittest
from os.path import dirname, join
from tempfile import TemporaryDirectory

import pandas as pd
from panthera.cheetah.cheetah_study import RollupStudy as Study
from panthera.rollup.rollup import Rollup

TESTDIR = dirname(os.path.abspath(__file__))
FIXTURE_DIR = join(dirname(TESTDIR), "fixtures")
FORMULA_TESTDIR = join(FIXTURE_DIR, "formulas")


class TestRollup(unittest.TestCase):
    def setUp(self):
        self.specs = [
            dict(name="echoone", exe="/bin/echo", parameters=["one"]),
            dict(name="echotwo", exe="/bin/echo", parameters=["two"]),
        ]
        self.workloads = [
            dict(name="999.hello", file="999.hello.is.xz", workload="hello", suite="greetings", weight=1.0),
            dict(name="999.goodbye_7", file="999.goodbye_7.is.xz", workload="goodbye", suite="greetings", weight=0.3),
            dict(name="999.goodbye_23", file="999.goodbye_23.is.xz", workload="goodbye", suite="greetings", weight=0.7),
            dict(name="1000.python_07", file="1000.python_07.is.xz", workload="python", suite="snakes", weight=1.0),
        ]
        self.study = Study(join(FIXTURE_DIR, "studies", "echotest"), self.specs, self.workloads, workload_dir=".")

    def test_constructor(self):
        r = Rollup.from_study(self.study)
        df, meta = r.create_dataframe()
        df = df.set_index(["configuration"], append=True)
        self.assertEqual(7, len(df))
        self.assertEqual(11, df["foo"]["999.hello"]["echoone"])
        self.assertEqual(29, df["foo"]["999.goodbye_7"]["echoone"])
        self.assertEqual(7, df["foo"]["999.goodbye_23"]["echoone"])
        self.assertEqual(12, df["foo"]["1000.python_07"]["echoone"])
        self.assertEqual(22, df["foo"]["999.hello"]["echotwo"])
        self.assertEqual(58, df["foo"]["999.goodbye_7"]["echotwo"])
        self.assertEqual(14, df["foo"]["999.goodbye_23"]["echotwo"])
        self.assertNotIn("echotwo", df["foo"]["1000.python_07"])  # log reports that it failed
        self.assertEqual(29, len(meta))
        self.assertIn("suite", meta)

    def test_weighting_grouping(self):
        r = Rollup.from_study(self.study)
        df = r.rollup(by="workload").reset_index().set_index(["workload", "configuration"])
        self.assertEqual(5, len(df))
        self.assertEqual(11, df["foo"]["hello"]["echoone"])
        self.assertAlmostEqual(13.6, df["foo"]["goodbye"]["echoone"])
        self.assertEqual(12, df["foo"]["python"]["echoone"])
        self.assertAlmostEqual(3.5, df["bar"]["goodbye"]["echoone"])
        self.assertEqual(22, df["foo"]["hello"]["echotwo"])
        self.assertAlmostEqual(27.2, df["foo"]["goodbye"]["echotwo"])
        self.assertAlmostEqual(3.5, df["bar"]["goodbye"]["echotwo"])
        self.assertNotIn("echotwo", df["foo"]["python"])
        self.assertEqual("greetings", df["suite"]["goodbye"]["echoone"])

        df = r.rollup(by="suite").reset_index().set_index(["suite", "configuration"])
        self.assertEqual(3, len(df))
        self.assertAlmostEqual(12.23, df["foo"]["greetings"]["echoone"], places=2)
        self.assertAlmostEqual(12, df["foo"]["snakes"]["echoone"])
        self.assertAlmostEqual(0.0, df["bar"]["greetings"]["echoone"])
        self.assertAlmostEqual(24.46, df["foo"]["greetings"]["echotwo"], places=2)
        self.assertNotIn("echotwo", df["foo"]["snakes"])

    def test_run_metrics(self):
        r = Rollup.from_study(self.study, run_metrics=True)
        df, _meta = r.create_dataframe()
        df = df.set_index(["configuration"], append=True)
        self.assertEqual(26, df["cputime"]["999.hello"]["echoone"])
        self.assertEqual(32, df["walltime"]["999.hello"]["echoone"])
        self.assertAlmostEqual(159.66, df["warmup_memory"]["999.hello"]["echoone"])
        self.assertAlmostEqual(159.66, df["measurement_memory"]["999.hello"]["echoone"])

    def test_formula(self):
        r = Rollup.from_study(self.study)
        r.read_formula_file(join(FORMULA_TESTDIR, "formulas_test.py"))
        df = r.rollup(by="trace").reset_index().set_index(["trace", "configuration"])
        self.assertEqual(11 / 4, df["fpc"]["999.hello"]["echoone"])

    def test_formula_inclusion(self):
        r = Rollup.from_study(self.study)
        r.read_formula_file(join(FORMULA_TESTDIR, "inclusion_test.py"))
        df = r.rollup(by="trace").reset_index().set_index(["trace", "configuration"])
        self.assertEqual(11 / 4, df["fpc"]["999.hello"]["echoone"])
        self.assertEqual(397, df["time_diff"]["999.hello"]["echoone"])

    def test_formula_inclusion_scope(self):
        r = Rollup.from_study(self.study)
        r.read_formula_file(join(FORMULA_TESTDIR, "inclusion_scope_test.py"))
        df = r.rollup(by="trace").reset_index().set_index(["trace", "configuration"])
        self.assertEqual(11 / 4, df["xxx.fpc"]["999.hello"]["echoone"])
        self.assertEqual(11 / 4, df["yyy.fpc"]["999.hello"]["echoone"])
        self.assertEqual(397, df["time_diff"]["999.hello"]["echoone"])

    def test_weighted_grouped_formula(self):
        r = Rollup.from_study(self.study)
        r.read_formula_file(join(FORMULA_TESTDIR, "formulas_test.py"))
        df = r.rollup(by="workload").reset_index().set_index(["workload", "configuration"])
        self.assertAlmostEqual(2.75, df["fpc"]["hello"]["echoone"])
        self.assertAlmostEqual(2.0, df["fpc"]["goodbye"]["echoone"])
        self.assertAlmostEqual(0.85714, df["fpc"]["python"]["echoone"], places=4)
        self.assertAlmostEqual(5.5, df["fpc"]["hello"]["echotwo"])
        self.assertAlmostEqual(4.0, df["fpc"]["goodbye"]["echotwo"])
        self.assertNotIn("echotwo", df["fpc"]["python"])

        df = r.rollup(by="suite").reset_index().set_index(["suite", "configuration"])
        self.assertAlmostEqual(2.3452, df["fpc"]["greetings"]["echoone"], places=4)
        self.assertAlmostEqual(0.85714, df["fpc"]["snakes"]["echoone"], places=4)
        self.assertAlmostEqual(4.6904, df["fpc"]["greetings"]["echotwo"], places=4)
        self.assertNotIn("echotwo", df["fpc"]["snakes"])

    def test_rollup_timeseries(self):
        r = Rollup.from_study(self.study, timeseries=True)
        self.assertEqual(len(r.timeseries), 7)

        r.read_formula_file(join(FORMULA_TESTDIR, "formulas_test.py"))
        with TemporaryDirectory() as output_directory:
            r.rollup_timeseries(output_directory=output_directory, workers=2)
            self.assertEqual(len(os.listdir(output_directory)), 2)
            ts = sorted(glob.glob(os.path.join(output_directory, "**", "*.ts.csv.gz"), recursive=True))
            self.assertEqual(len(ts), 7)

            ts_rollup = pd.read_csv(ts[0])
            self.assertIn("fpc", ts_rollup.columns)

    def test_rollup_summary_csv(self):
        r = Rollup.from_summaries(join(FIXTURE_DIR, "studies", "echotest", "echoone", "999.hello.ts.csv.gz"))
        r.read_formula_file(join(FORMULA_TESTDIR, "formulas_test.py"))

        df = r.rollup(by="trace")
        self.assertEqual(11 / 4, df["fpc"]["999.hello"])
