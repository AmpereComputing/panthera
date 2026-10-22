# Copyright (c) 2024-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import shutil
import subprocess
import unittest
from glob import glob
from os.path import dirname, join
from tempfile import TemporaryDirectory

import pandas as pd
from panthera import install_dir

panthera_py = os.path.realpath(join(install_dir, "bin", "panthera"))
FIXTURES_DIR = join(dirname(dirname(__file__)), "fixtures")
ES_STUDY_DIR = join(FIXTURES_DIR, "studies", "es_study")
RTL_CSV_PATH = join(FIXTURES_DIR, "rollups", "rtl_rollup.csv")
PANTHERA_CSV_PATH = join(FIXTURES_DIR, "rollups", "panthera_rollup.csv")
IGNORE_PATH = join(FIXTURES_DIR, "json", "ignore.json")


class TestGenerateSCurve(unittest.TestCase):
    def setUp(self):
        self.output_dir = TemporaryDirectory()
        self.rtl_es_dir = self.output_path("rtl_es")
        self.panthera_es_dir = self.output_path("panthera_es_dir")
        shutil.copytree(ES_STUDY_DIR, self.rtl_es_dir)
        shutil.copytree(ES_STUDY_DIR, self.panthera_es_dir)

    def tearDown(self):
        self.output_dir.cleanup()

    def run_generate_scurve(self, *args):
        cmd = [
            panthera_py, "snippets.generate_SCurve",
            self.scurve_path,
            PANTHERA_CSV_PATH,
            RTL_CSV_PATH,
            self.output_path("temp"),
            "--RTL_regr", self.rtl_es_dir,
            "--panthera_es_folder", self.panthera_es_dir,
            "--es1_es_same_inum_start", "50",
            "--es2_es_same_inum_start", "50",
            "--max_instructions", "100",
            "--debug",
        ] + list(args)  # fmt: skip

        return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True)

    def output_path(self, *args):
        return os.path.join(self.output_dir.name, *args)

    @property
    def scurve_path(self):
        return self.output_path("scurve.csv")

    def test_help(self):
        self.run_generate_scurve("--help")

    def read_scurve(self):
        self.assertTrue(os.path.isfile(self.scurve_path))
        return pd.read_csv(self.scurve_path).set_index("trace")

    @property
    def traces_to_debug(self):
        return self.output_path("temp", "traces_to_debug.txt")

    def read_traces_to_debug(self):
        self.assertTrue(os.path.isfile(self.traces_to_debug))
        with open(self.traces_to_debug, "r") as f:
            return list(map(str.strip, f.readlines()))

    def test_generate_basic(self):
        self.run_generate_scurve()
        scurve = self.read_scurve()
        self.assertEqual(scurve.shape, (3, 5))
        traces = self.read_traces_to_debug()
        self.assertEqual(traces, ["something_0", "something_2"])

    def test_different_threshold(self):
        self.run_generate_scurve("--lower_threshold", "1", "--upper_threshold", "1")
        traces = self.read_traces_to_debug()
        self.assertEqual(traces, ["something_0", "something_1", "something_2"])

    def test_generate_mismatch_check(self):
        output = self.run_generate_scurve("--mismatch_check").stdout.decode()
        scurve = self.read_scurve()
        self.assertEqual(scurve.shape, (3, 6))
        # Mismatch because event/data names are different
        self.assertTrue((scurve["is_same_code?"] == "Mismatch").all())
        # Check max-instruction determination
        self.assertRegex(output, r"es_same_path.*?something_0.*?es1-inum-start 50.*?max-instructions 100")
        self.assertRegex(output, r"es_same_path.*?something_1.*?es1-inum-start 50.*?max-instructions 100")
        self.assertRegex(output, r"es_same_path.*?something_2.*?es1-inum-start 10.*?max-instructions 40")

    def test_generate_speculation_check(self):
        self.run_generate_scurve("--RTL_speculation_check")
        scurve = self.read_scurve()
        self.assertEqual(scurve.shape, (3, 6))
        # FIXME: not really testing that it was done
        self.assertTrue((scurve["speculative #"] == 0).all())

    def test_ignore(self):
        self.run_generate_scurve("--ignore", IGNORE_PATH)
        scurve = self.read_scurve()
        self.assertEqual(scurve.shape, (2, 5))
        traces = self.read_traces_to_debug()
        self.assertEqual(traces, ["something_0"])
