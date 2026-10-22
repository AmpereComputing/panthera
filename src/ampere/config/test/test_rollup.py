# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import unittest
from glob import glob
from tempfile import TemporaryDirectory

from pandas import read_csv
from panthera import install_dir
from panthera.rollup.__main__ import main as rollup

TESTDIR = os.path.dirname(os.path.abspath(__file__))


class TestRollupConfigs(unittest.TestCase):
    def verify_config(self, name, csv=None):
        path = os.path.join(install_dir, "config", "rollup", name)
        csv = csv or os.path.join(TESTDIR, "fixtures", "panthera.test.csv")
        with TemporaryDirectory() as output:
            rollup(
                [
                    "--threads",
                    "1",
                    "--keep-factored",
                    "--summary-csv",
                    csv,
                    "--formula",
                    path,
                    "--outdir",
                    output,
                ]
            )
            rollups = glob(os.path.join(output, "*.csv"))
            original = read_csv(csv).shape
            self.assertEqual(len(rollups), 1)
            for rf in rollups:
                rolled_up = read_csv(rf).shape
                self.assertEqual(rolled_up[0], original[0])
                self.assertGreater(rolled_up[1], original[1])

    def test_regress_formula(self):
        self.verify_config("regress_formula.py")

    def test_rollup_formula(self):
        self.verify_config("rollup_formula.py")

    def test_rtl_rollup_formula(self):
        self.verify_config(
            "rtl_rollup_formula.py",
            csv=os.path.join(TESTDIR, "fixtures", "rtl.test.csv"),
        )

    def test_bobcat_rollup_formula(self):
        self.verify_config(
            "bobcat_formula.py",
            csv=os.path.join(TESTDIR, "fixtures", "bobcat.test.csv"),
        )
