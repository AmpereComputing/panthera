# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import subprocess
import unittest
from os.path import dirname, join

from panthera import install_dir

panthera_py = os.path.realpath(join(install_dir, "bin", "panthera"))
ES_FILE = join(dirname(dirname(__file__)), "test_events.es")


class TestESSamePath(unittest.TestCase):
    @classmethod
    def run_es_same_path(cls, *args):
        cmd = [
            panthera_py,
            "snippets.es_same_path",
        ] + list(args)

        return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True)

    def test_help(self):
        self.run_es_same_path("--help")

    def run_same_es(self, *args):
        return self.run_es_same_path(
            ES_FILE, ES_FILE,
            "--es1-instr-commit-event", "core.gpc.instruction_commit",
            "--es2-instr-commit-event", "core.gpc.instruction_commit",
            "--es1-instr-commit-pc", "core.gpc.commit_pc",
            "--es2-instr-commit-pc", "core.gpc.commit_pc",
            "--es1-instr-commit-inum", "core.gpc.inum",
            "--es2-instr-commit-inum", "core.gpc.inum",
            *args,
        )  # fmt: skip

    def test_same_es(self):
        self.run_same_es("--max-instructions", "100")

    def test_same_es_unexpected_end(self):
        with self.assertRaises(subprocess.CalledProcessError):
            result = self.run_same_es()
            self.assertRegex(result.stdout.decode(), r"Validated only 100.*expected 30000")

    def test_missing_commit(self):
        with self.assertRaises(subprocess.CalledProcessError):
            result = self.run_same_es("--es1-instr-commit-event", "core.gpc.missing")
            self.assertRegex(result.stdout.decode(), r"Failed to find core.gpc.missing")

    def test_missing_commit_pc(self):
        with self.assertRaises(subprocess.CalledProcessError):
            result = self.run_same_es("--es1-instr-commit-pc", "core.gpc.missing_pc")
            self.assertRegex(result.stdout.decode(), r"Failed to find core.gpc.missing_pc")

    def test_missing_commit_inum(self):
        with self.assertRaises(subprocess.CalledProcessError):
            result = self.run_same_es("--es1-instr-commit-inum", "core.gpc.missing_inum")
            self.assertRegex(result.stdout.decode(), r"Failed to find core.gpc.missing_inum")
