# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import subprocess
import tempfile
import unittest
from os.path import dirname, join

from panthera import install_dir

panthera_py = os.path.realpath(join(install_dir, "bin", "panthera"))

test_dir = dirname(__file__)


class TestSim(unittest.TestCase):
    def run_sim(self, *args, allow_failure=False, **kwargs):
        with tempfile.TemporaryDirectory() as tmpdirname:
            cmd = [panthera_py, "sim"] + list(args)
            pan = subprocess.run(
                cmd, check=not allow_failure, cwd=tmpdirname, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **kwargs
            )

            return pan.stdout.decode()

    def nothing(self):
        return os.path.join(test_dir, "nothing.sh")

    def verify_complete(self, out):
        return self.assertRegex(out, "Simulation stopped by user")

    def test_run(self):
        out = self.run_sim(
            self.nothing(),
            "--",
            self.nothing(),
        )
        self.verify_complete(out)

    def test_run_no_args_uses_amperesys(self):
        out = self.run_sim("-dump_settings", "true", allow_failure=True)
        self.assertRegex(out, r".*/amperesys -event_stream")
        self.assertRegex(out, r"dump_settings true")

    def test_failing_simulator(self):
        out = self.run_sim(os.path.join(test_dir, "die.sh"), allow_failure=True)
        self.assertRegex(out, "ERROR: .* simulator died with signal 11")

    def test_gdb(self):
        out = self.run_sim("--gdb", self.nothing(), "--", self.nothing(), input="r\rq\rY\r".encode())
        self.assertRegex(out, r"--args .*/test/nothing.sh")
        self.verify_complete(out)

    def test_gdb_args(self):
        out = self.run_sim(
            "--gdb", "-ex 'catch throw'", self.nothing(), "--", self.nothing(), input="r\rq\rY\r".encode()
        )
        self.assertRegex(out, r"Catchpoint \d.*\(throw\)")
        self.verify_complete(out)

    def test_gdb_evp(self):
        out = self.run_sim("--gdb-evp", self.nothing(), "--", self.nothing(), input="r\rq\rY\r".encode())
        self.assertRegex(out, r".*/test/nothing.sh --in_fd")
        self.assertRegex(out, r"--args .*/test/nothing.sh")
        self.verify_complete(out)
