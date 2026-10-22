# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import logging
import unittest

import pandas as pd
from panthera.rollup.metric_formula import MetricFormula


class TestMetricFormula(unittest.TestCase):
    def test_get_one_level_attribute(self):
        df = dict(foo=1)
        mf = MetricFormula(df)
        self.assertEqual(1, mf.foo)

    def test_get_two_level_attribute(self):
        df = {"foo.bar": 1}
        mf = MetricFormula(df)
        self.assertEqual(1, mf.foo.bar)

    def test_get_three_level_attribute(self):
        df = {"foo.bar.baz": 1}
        mf = MetricFormula(df)
        self.assertEqual(1, mf.foo.bar.baz)

    def test_set_one_level_attribute(self):
        mf = MetricFormula({})
        mf.foo = 1
        self.assertEqual(1, mf.df["foo"])

    def test_set_two_level_attribute(self):
        mf = MetricFormula({})
        mf.foo.bar = 1
        self.assertEqual(1, mf.df["foo.bar"])

    def test_set_three_level_attribute(self):
        mf = MetricFormula({})
        mf.foo.bar.baz = 1
        self.assertEqual(1, mf.df["foo.bar.baz"])

    def test_set_many_three_level_attributes(self):
        df = pd.DataFrame([[0], [0]])
        mf = MetricFormula(df)
        mf.foo.bar.baz = 1
        mf.foo.foo.baz = 5
        mf.foo.bar.bazzle = 3
        mf.foo.foo.bazzle = 6
        self.assertEqual(2, mf.df["foo.bar.baz"].sum())
        self.assertEqual(6, mf.df["foo.bar.bazzle"].sum())
        self.assertEqual(10, mf.df["foo.foo.baz"].sum())
        self.assertEqual(12, mf.df["foo.foo.bazzle"].sum())

    def test_create_one_level_metric_by_division(self):
        df = {"instructions": 35, "cycles": 5}
        mf = MetricFormula(df)
        mf.ipc = mf.instructions / mf.cycles
        self.assertEqual(7, mf.ipc)
        self.assertEqual(7, df["ipc"])

    def test_create_two_level_metric_by_division(self):
        df = {"core.instructions": 35, "core.cycles": 5}
        mf = MetricFormula(df)
        mf.core.ipc = mf.core.instructions / mf.core.cycles
        self.assertEqual(7, mf.core.ipc)
        self.assertEqual(7, df["core.ipc"])

    def test_create_three_level_metric_by_division(self):
        df = {"proc0.core0.instructions": 35, "proc0.core0.cycles": 5}
        mf = MetricFormula(df)
        mf.proc0.core0.ipc = mf.proc0.core0.instructions / mf.proc0.core0.cycles
        self.assertEqual(7, mf.proc0.core0.ipc)
        self.assertEqual(7, df["proc0.core0.ipc"])

    def test_local_formula(self):
        df = {"core.instructions": 35, "core.cycles": 5}
        mf = MetricFormula(df)
        core = mf.core
        core.ipc = core.instructions / core.cycles
        self.assertEqual(7, mf.core.ipc)
        self.assertEqual(7, df["core.ipc"])

    def test_contains(self):
        df = {"core.instructions": 35, "core.cycles": 5}
        mf = MetricFormula(df)
        self.assertTrue("core.instructions" in mf)
        self.assertFalse("core" in mf)
        self.assertFalse("foo" in mf)
        core = mf.core
        self.assertTrue("instructions" in core)
        self.assertTrue("cycles" in core)
        self.assertFalse("core.cycles" in core)
        self.assertFalse("bar" in core)

    def test_missing(self):
        logger = logging.getLogger()
        df = {"core.cycles": 5}
        mf = MetricFormula(df)

        with self.assertLogs(logger, "WARNING") as cm:
            self.assertEqual(5, mf.core.stalls + mf.core.cycles)

        with self.assertLogs(logger, "WARNING") as cm:
            self.assertEqual(5, mf.core.cycles + mf.core.stalls)

        with self.assertLogs(logger, "WARNING") as cm:
            self.assertEqual(-5, mf.core.stalls - mf.core.cycles)

        with self.assertLogs(logger, "WARNING") as cm:
            self.assertEqual(5, mf.core.cycles - mf.core.stalls)

        with self.assertLogs(logger, "WARNING") as cm:
            self.assertEqual(0, mf.core.stalls * mf.core.cycles)

        with self.assertLogs(logger, "WARNING") as cm:
            self.assertEqual(0, mf.core.stalls / mf.core.cycles)

        with self.assertLogs(logger, "WARNING") as cm:
            self.assertEqual(0, mf.core.stalls >> mf.core.cycles)

        with self.assertLogs(logger, "WARNING") as cm:
            self.assertEqual(0, mf.core.stalls << mf.core.cycles)

    def test_pandas_fragmentation(self):
        df = pd.DataFrame([[0], [0]])
        mf = MetricFormula(df)
        for i in range(1000):
            getattr(mf, f"foo_{i}").bar = i

        for i in range(1000):
            self.assertEqual(i * 2, mf.df[f"foo_{i}.bar"].sum())
