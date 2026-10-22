# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import json
import os
import subprocess
import unittest
from glob import glob
from os.path import dirname, join
from tempfile import TemporaryDirectory

from panthera import install_dir

panthera_py = os.path.realpath(join(install_dir, "bin", "panthera"))
FIXTURES_DIR = join(dirname(dirname(__file__)), "fixtures")
TRACELISTS_DIR = join(FIXTURES_DIR, "tracelists")


class TestConsolidateManifest(unittest.TestCase):
    def setUp(self):
        self.output_dir = TemporaryDirectory()
        self.consolidated = self.output_path("consolidated")

    def tearDown(self):
        self.output_dir.cleanup()

    def output_path(self, *args):
        return os.path.join(self.output_dir.name, *args)

    def tracelist(self, name):
        return os.path.join(TRACELISTS_DIR, name, "manifest.json")

    def load_tracelist(self, path):
        with open(path, "r") as jf:
            return json.load(jf)

    @classmethod
    def run_consolidate(cls, *args):
        cmd = [panthera_py, "tracing.consolidate_manifest"] + list(args)
        return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True)

    def test_basic(self):
        self.run_consolidate(
            self.consolidated,
            self.tracelist("some_traces"),
            self.tracelist("more_traces"),
        )
        self.assertTrue(os.path.isdir(self.consolidated))
        self.assertEqual(len(glob(os.path.join(self.consolidated, "**", "*"))), 8)

        tracelist = os.path.join(self.consolidated, "manifest.json")
        self.assertTrue(os.path.isfile(tracelist))
        traces = self.load_tracelist(tracelist)
        self.assertEqual([t["name"] for t in traces], ["trace_1", "trace_0"])
        self.assertEqual(sum(len(t) for t in traces), 30)

    def test_dryrun(self):
        self.run_consolidate(
            self.consolidated, self.tracelist("some_traces"), self.tracelist("more_traces"), "--dryrun"
        )
        self.assertFalse(os.path.isdir(self.consolidated))

    def test_missing(self):
        with self.assertRaises(subprocess.CalledProcessError):
            self.run_consolidate(
                self.consolidated,
                self.tracelist("missing_traces"),
                self.tracelist("more_traces"),
            )

    def test_skip_missing(self):
        self.run_consolidate(
            self.consolidated, self.tracelist("missing_traces"), self.tracelist("more_traces"), "--skip-missing"
        )
        self.assertTrue(os.path.isdir(self.consolidated))
        self.assertEqual(len(glob(os.path.join(self.consolidated, "**", "*"))), 7)

        tracelist = os.path.join(self.consolidated, "manifest.json")
        self.assertTrue(os.path.isfile(tracelist))
        traces = self.load_tracelist(tracelist)
        self.assertEqual(sum(len(t) for t in traces), 28)
        self.assertNotIn("pan_branch_warmup_file", traces[0])
        self.assertNotIn("pan_branch_warmup_file_sha256sum", traces[0])

    def test_skip_duplicated(self):
        self.run_consolidate(
            self.consolidated,
            self.tracelist("some_traces"),
            self.tracelist("some_traces"),
        )
        self.assertEqual(len(glob(os.path.join(self.consolidated, "**", "*"))), 4)

        tracelist = os.path.join(self.consolidated, "manifest.json")
        self.assertTrue(os.path.isfile(tracelist))
        traces = self.load_tracelist(tracelist)
        self.assertEqual([t["name"] for t in traces], ["trace_1"])
        self.assertEqual(sum(len(t) for t in traces), 15)
