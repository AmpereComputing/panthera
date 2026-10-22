# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import tempfile
import unittest
from unittest.mock import patch

import pandas as pd
from panthera.report import helpers


class TestRollupHelpers(unittest.TestCase):
    def setUp(self):
        d = {
            "trace": ["a", "b", "a", "b"],
            "configuration": ["x", "x", "y", "y"],
            "workload": ["m", "p", "m", "p"],
            "suite": ["q", "q", "q", "q"],
            "core.ipc": [1.0, 5.0, 1.25, 10],
        }
        self.df = pd.DataFrame(d)

    def test_rollup_and_pivot(self):
        rollup = helpers.rollup_and_pivot(self.df, "core.ipc")
        self.assertEqual(1.0, rollup["x"]["a"])
        self.assertEqual(1.25, rollup["y"]["a"])
        self.assertEqual(5.0, rollup["x"]["b"])
        self.assertEqual(10.0, rollup["y"]["b"])

    @patch.object(helpers, "config_list", ["y", "x", "z"], create=True)
    def test_rollup_and_pivot_explicit_order(self):
        rollup = helpers.rollup_and_pivot(self.df, "core.ipc")
        self.assertEqual(rollup.columns.tolist(), ["y", "x"])

    def test_speedup(self):
        speedup = helpers.speedup(self.df, "core.ipc")
        self.assertEqual(1.25, speedup["y"]["a"])
        self.assertAlmostEqual(2.0, speedup["y"]["b"], places=2)

    @patch.object(helpers, "config_list", ["y", "x", "z"], create=True)
    def test_speedup_explicit_order(self):
        speedup = helpers.speedup(self.df, "core.ipc")
        self.assertEqual(speedup.columns.tolist(), ["y", "x"])

    def test_create_plot(self):
        output_dir = tempfile.TemporaryDirectory()
        with patch.object(helpers, "output_dir", output_dir.name) as _output_dir:
            helpers.rollup_index = "trace"
            helpers.create_plot(self.df, "test_plot.svg")
            self.assertTrue(os.path.isfile(os.path.join(output_dir.name, "test_plot.trace.svg")))
