# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import json
import os
import unittest
from os.path import dirname, join
from tempfile import TemporaryDirectory

from panthera.tracing.filter_study_list import (
    assign_field_values,
    normalize_weights_within_workload,
    rename_field_values,
)

TESTDIR = dirname(os.path.abspath(__file__))
FIXTURE_DIR = join(dirname(TESTDIR), "fixtures")
TRACELIST_TESTDIR = join(FIXTURE_DIR, "tracelists")


class TestFilterStudyList(unittest.TestCase):
    def load_tracelist(self, name):
        with open(os.path.join(TRACELIST_TESTDIR, name), "r") as jf:
            return json.load(jf)

    def setUp(self):
        self.tracelist = self.load_tracelist("basic.json")

    def test_normalize(self):
        normalize_weights_within_workload(self.tracelist)
        self.assertAlmostEqual(self.tracelist[1]["weight"], 0.555, 2)
        self.assertAlmostEqual(self.tracelist[2]["weight"], 0.445, 2)

    def test_rename(self):
        rename_field_values([("name", r"goodbye_(\d+)", r"goodbye-\1")], self.tracelist)
        self.assertEqual(self.tracelist[1]["name"], "999.goodbye-23")

    def test_assign(self):
        assign_field_values([("workload", "999.goodbye", "suite", "something")], self.tracelist)
        self.assertEqual(self.tracelist[1]["suite"], "something")
