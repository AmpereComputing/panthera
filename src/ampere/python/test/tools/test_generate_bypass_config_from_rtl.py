# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import json
import os
import subprocess
import unittest
from os.path import dirname, join

from panthera import install_dir

panthera_py = os.path.realpath(join(install_dir, "bin", "panthera"))
FIXTURES_DIR = join(dirname(dirname(__file__)), "fixtures")
RTL_DIR = join(FIXTURES_DIR, "rtl")


class TestGenerateBypassConfigFromRTL(unittest.TestCase):
    @classmethod
    def run_generate_bypass_config_from_rtl(cls, *args):
        cmd = [
            panthera_py,
            f"tools.generate_bypass_config_from_rtl",
        ] + list(args)

        return subprocess.run(cmd, stdout=subprocess.PIPE, check=True)

    def test_basic(self):
        out = self.run_generate_bypass_config_from_rtl(os.path.join(RTL_DIR, "bypass_levels.sv"))
        config = json.loads(out.stdout.decode())
        expected = {
            "latencies": {
                "BU": {"unit": "BU", "levels": {"LATE": 3, "LOCAL": 4, "NORMAL": 5}},
                "CU2": {
                    "unit": "CU",
                    "unit_suffix": 2,
                    "levels": {"LOCAL": 2, "NORMAL": 3},
                },
            }
        }
        self.assertEqual(config, expected)
