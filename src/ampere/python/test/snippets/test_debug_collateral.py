# Copyright (c) 2024-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import shutil
import subprocess
import unittest
from glob import glob
from os.path import dirname, join
from tempfile import TemporaryDirectory

from panthera import install_dir

panthera_py = os.path.realpath(join(install_dir, "bin", "panthera"))
FIXTURES_DIR = join(dirname(dirname(__file__)), "fixtures")
ES_STUDY_DIR = join(FIXTURES_DIR, "studies", "es_study")


class TestDebugCollateral(unittest.TestCase):
    def setUp(self):
        self.output_dir = TemporaryDirectory()
        self.rtl_es_dir = self.output_path("rtl_es")
        self.panthera_es_dir = self.output_path("panthera_es_dir")
        shutil.copytree(ES_STUDY_DIR, self.rtl_es_dir)
        shutil.copytree(ES_STUDY_DIR, self.panthera_es_dir)

    def tearDown(self):
        self.output_dir.cleanup()

    @classmethod
    def run_debug_collateral_command(cls, *args, command="debug_collateral"):
        cmd = [
            panthera_py, f"snippets.{command}",
            "--debug",
            "--es_progress_event", "core.gpc.instruction_commit",
        ] + list(args)  # fmt: skip

        return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True)

    @classmethod
    def run_debug_collateral(cls, *args):
        return cls.run_debug_collateral_command(
            *args,
            "--es_pc_metric", "core.gpc.commit_pc",
            "--es_inum_metric", "core.gpc.inum",
        )  # fmt: skip

    def output_path(self, *args):
        return os.path.join(self.output_dir.name, *args)

    def make_traces_to_debug(self, traces):
        path = self.output_path("traces.txt")
        with open(path, "w") as tf:
            tf.write("\n".join(traces))
        return path

    @property
    def output_traces(self):
        return {os.path.basename(p) for p in glob(os.path.join(self.output_dir.name, "something*"))}

    def verify_trace_files(self, trace, configs=("RTL", "Panthera"), instructions=None):
        trace_dir = self.output_path(trace)
        files = set()
        for name in configs:
            for suffix in (".all.es.xz", ".es.xz", ".richasm.txt", ".rob.json.gz", ".json.gz"):
                files.add(f"{name}_{trace}{suffix}")

            if len(configs) > 1:
                for suffix in (
                    ".out.csv",
                    ".out.summary.txt",
                    ".out.pdtest.txt",
                    ".out_progressive_drift.png",
                    ".out_group_by_pc_sort_by_pc.csv",
                    ".out_group_by_pc_sort_by_drift.csv",
                ):
                    files.add(f"{trace}_{trace}.match{suffix}")

        self.assertSetEqual(set(os.listdir(trace_dir)), files)

        if instructions is not None:
            with open(os.path.join(trace_dir, f"{trace}_{trace}.match.out.summary.txt"), "r") as f:
                self.assertEqual(instructions, int(f.read().split()[0]))

    def verify_links(self, trace):
        trace_dir = self.output_path(trace)
        for name in set(os.listdir(trace_dir)):
            if os.path.islink(name):
                link_path = os.path.join(os.path.dirname(name), os.readlink(name))
                self.assertTrue(os.path.exists(link_path))

    def test_help(self):
        self.run_debug_collateral("--help")

    def test_debug_collateral(self):
        self.run_debug_collateral(
            "--rtl_es_folder", self.rtl_es_dir,
            "--panthera_es_folder", self.panthera_es_dir,
            "--output_dir", self.output_dir.name,
            "--traces_to_debug_path", self.make_traces_to_debug(["something_1"]),
            "--es_rtl_ts_start", "50",
            "--es_rtl_ts_end", "100",
            "--es_panthera_ts_start", "50",
            "--es_panthera_ts_end", "100",
        )  # fmt: skip
        self.assertEqual(len(glob(os.path.join(self.rtl_es_dir, "CROPPED*"))), 1)
        self.assertEqual(len(glob(os.path.join(self.panthera_es_dir, "CROPPED*"))), 1)
        self.assertSetEqual(self.output_traces, {"something_1"})
        self.verify_trace_files("something_1", instructions=49)
        self.verify_links("something_1")

    def test_debug_collateral_single(self):
        self.run_debug_collateral_command(
            "--name", "RTL",
            "--output_dir", self.output_dir.name,
            "--es_folder", self.rtl_es_dir,
            "--traces_to_debug_path", self.make_traces_to_debug(["something_1"]),
            "--es_start", "50",
            "--es_end", "100",
            command="singular_debug_collateral",
        )  # fmt: skip
        self.assertEqual(len(glob(os.path.join(self.rtl_es_dir, "CROPPED*"))), 1)
        self.assertEqual(len(glob(os.path.join(self.rtl_es_dir, "*.json.gz"))), 2)
        self.assertEqual(len(glob(os.path.join(self.rtl_es_dir, "*.richasm.txt"))), 1)

    def test_debug_collateral_single_shorter(self):
        output = self.run_debug_collateral_command(
            "--name", "RTL",
            "--output_dir", self.output_dir.name,
            "--es_folder", self.rtl_es_dir,
            "--traces_to_debug_path", self.make_traces_to_debug(["something_2"]),
            "--es_start", "50",
            "--es_end", "100",
            command="singular_debug_collateral",
        ).stdout.decode()  # fmt: skip
        self.assertRegex(output, r"Crop:.*?--start .*?=1 --stop .*?=50")
        self.assertEqual(len(glob(os.path.join(self.rtl_es_dir, "CROPPED*"))), 1)
        self.assertEqual(len(glob(os.path.join(self.rtl_es_dir, "*.json.gz"))), 2)
        self.assertEqual(len(glob(os.path.join(self.rtl_es_dir, "*.richasm.txt"))), 1)

    def test_debug_collateral_single_adis(self):
        self.run_debug_collateral_command(
            "--name", "RTL",
            "--output_dir", self.output_dir.name,
            "--es_folder", self.rtl_es_dir,
            "--traces_to_debug_path", self.make_traces_to_debug(["something_1"]),
            "--es_start", "50",
            "--es_end", "100",
            "--adis",
            command="singular_debug_collateral",
        )  # fmt: skip
        self.assertEqual(len(glob(os.path.join(self.rtl_es_dir, "*.richasm.txt"))), 1)

    def test_debug_collateral_single_log_files(self):
        self.run_debug_collateral_command(
            "--name", "RTL",
            "--output_dir", self.output_dir.name,
            "--es_folder", self.rtl_es_dir,
            "--traces_to_debug_path", self.make_traces_to_debug(["something_1"]),
            "--es_start", "50",
            "--es_end", "100",
            "--link", ".log",
            command="singular_debug_collateral",
        )  # fmt: skip
        self.assertEqual(len(glob(os.path.join(self.output_dir.name, "something_1", "*.log"))), 1)
        self.verify_links("something_1")

    def test_debug_collateral_single_infer_trace_path_from_log(self):
        self.run_debug_collateral_command(
            "--name", "Panthera",
            "--output_dir", self.output_dir.name,
            "--es_folder", self.panthera_es_dir,
            "--traces_to_debug_path", self.make_traces_to_debug(["something_0"]),
            "--es_start", "50",
            "--es_end", "100",
            command="singular_debug_collateral",
        )  # fmt: skip
        self.assertEqual(len(glob(os.path.join(self.output_dir.name, "something_0", "*.is.xz"))), 1)
        self.verify_links("something_0")
