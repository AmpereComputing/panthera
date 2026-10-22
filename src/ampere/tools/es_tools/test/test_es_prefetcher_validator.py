# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import subprocess
import unittest
from os.path import dirname

from panthera import install_dir

PANTHERA = os.path.realpath(os.path.join(install_dir, "bin", "panthera"))
VALIDATOR = os.path.realpath(os.path.join(install_dir, "bin", "es_prefetcher_validator"))
TEST_FIXTURE_DIR = os.path.join(dirname(dirname(dirname(dirname(os.path.realpath(__file__))))), "core", "test")


class TestPrefetcherValidator(unittest.TestCase):
    def run_and_log(self, *args, **kwargs):
        args = [PANTHERA, "sim", VALIDATOR] + list(args) + ["--", "+count", "-a", "+summarize"]
        return subprocess.run(args, check=True, stdout=subprocess.PIPE, **kwargs)

    def events_for(self, output):
        return dict(line.strip().split()[:2] for line in output.split("\n") if " " in line)

    def test_rogue(self):
        result = self.run_and_log(
            "-event_stream",
            os.path.join(TEST_FIXTURE_DIR, "549-fotonik3d-1.92839_00.trinity_rtl.es.xz"),
            "-top.l2c:cfg",
            os.path.join(install_dir, "config/model/core/prefetching/validator/rtl/l2p_bop.cfg"),
            "-top.l2c.prefetcher:cfg",
            os.path.join(install_dir, "config/model/core/trinity/l2c_prefetcher.cfg"),
            "-top.l2c.prefetcher:append-cfg",
            os.path.join(install_dir, "config/model/core/trinity/bop_quick_train.cfg"),
        )

        output = result.stdout.decode()
        events = self.events_for(output)
        self.assertEqual(events["top.l2c.prefetcher.reset_maxscore"], "1")
        self.assertEqual(events["top.l2c.prefetcher.reset_maxrounds"], "1")
        self.assertEqual(events["top.l2c.prefetcher.train"], "882")
        self.assertEqual(events["top.l2c.prefetcher.rrt_update"], "356")
