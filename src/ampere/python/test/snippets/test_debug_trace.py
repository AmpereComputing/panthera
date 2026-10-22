# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import subprocess
import unittest
from os.path import basename, dirname, join, splitext
from pathlib import Path
from tempfile import TemporaryDirectory

from panthera import install_dir

panthera_py = os.path.realpath(join(install_dir, "bin", "panthera"))
es_file = join(dirname(dirname(__file__)), "test_events.es")
es_filename = splitext(basename(es_file))[0]


class TestDebugTrace(unittest.TestCase):
    @classmethod
    def run_debug_trace(cls, *args):
        cmd = [panthera_py, "snippets.debug_trace"] + list(args)
        return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True)

    def verify_files(self, directory, expected):
        output_files = os.listdir(directory)
        self.assertEqual(set(output_files), expected)
        for output_file in output_files:
            self.assertGreater(os.path.getsize(join(directory, output_file)), 0)

    def test_help(self):
        self.run_debug_trace("--help")

    def test_rtl(self):
        with TemporaryDirectory() as tmp:
            self.run_debug_trace("--which", "rtl", "--output_dir", tmp, es_file)
            self.verify_files(
                tmp,
                {
                    f"{es_filename}.json.gz",
                    f"{es_filename}.richasm.txt",
                },
            )

    def test_panthera(self):
        with TemporaryDirectory() as tmp:
            self.run_debug_trace("--which", "panthera", "--output_dir", tmp, es_file)
            self.verify_files(
                tmp,
                {
                    f"{es_filename}.json.gz",
                    f"{es_filename}.richasm.txt",
                },
            )
