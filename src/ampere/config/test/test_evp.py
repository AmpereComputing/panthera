# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import subprocess
import unittest

from panthera import install_dir

TESTDIR = os.path.dirname(os.path.abspath(__file__))


class TestEVPConfigs(unittest.TestCase):
    def run_evp(self, config):
        return subprocess.run(
            [
                os.path.join(install_dir, "bin", "evp"),
                "--es",
                os.path.join(TESTDIR, "fixtures", "test_events.es"),
                f"@{config}.cfg",
            ],
            check=True,
        )

    def verify_config(self, name):
        self.run_evp(name)

    def test_base(self):
        self.verify_config("base")

    # FIXME: get better test ES file, many normal events are not present in it
    # def test_rtl_base(self):
    #    self.verify_config("rtl/base")

    def test_bobcat(self):
        self.verify_config("bobcat")
