# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import subprocess
import unittest
from os.path import dirname, join

from panthera import install_dir

panthera_py = os.path.realpath(join(install_dir, "bin", "panthera"))
FIXTURES_DIR = join(dirname(dirname(__file__)), "fixtures")
STUDIES_DIR = join(FIXTURES_DIR, "studies")


class TestErrors(unittest.TestCase):
    @classmethod
    def run_errors(cls, *args):
        cmd = [panthera_py, "cheetah.errors"] + list(args)
        return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True)

    def study(self, *args):
        return join(STUDIES_DIR, *args)

    def test_bucket_directory(self):
        output = self.run_errors(self.study("echotest")).stdout.decode()
        self.assertRegex(output, "1 jobs have errors")
        self.assertRegex(output, "1 jobs .* Error.*Simulation brain dead")

    def test_bucket_inner_directory(self):
        output = self.run_errors(self.study("echotest", "echotwo")).stdout.decode()
        self.assertRegex(output, "1 jobs have errors")
        self.assertRegex(output, "1 jobs .* Error.*Simulation brain dead")

        output = self.run_errors(self.study("echotest", "echoone")).stdout.decode()
        self.assertRegex(output, "All jobs passed")
