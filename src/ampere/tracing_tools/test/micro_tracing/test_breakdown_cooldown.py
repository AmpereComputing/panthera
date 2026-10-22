# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import unittest

from . import EXEC_TMP, TestMicroTracing


class TestMicroTracingBreakdownCooldown(TestMicroTracing):
    micro_name = "bm_mem_lat_vec"
    output_name = micro_name
    stream_name = "bm_mem_lat_vec_0.is.xz"
    stats_type = "breakdown"

    def command(self, micro_path, kernel_path):
        return [
            "--environment",
            "BENCHMARK_WARMUP_ITERATIONS=50",
            "BENCHMARK_ITERATIONS=50",
            "BENCHMARK_COOLDOWN_ITERATIONS=50",
            "--cooldown",
            "5000",
            "--execute",
            micro_path + " --gtest_filter=LatMemVecRdTests/RandomStridedVector.Reads/0",
            "--kernel",
            kernel_path,
        ]

    def check_micro_trace_output(self, checkpoint):
        expected = [
            self.micro_name,
            f"{self.micro_name}_0.is.xz",
            f"{self.micro_name}_0.ms.xz",
            f"{self.micro_name}_0.bs.xz",
        ]
        if TestMicroTracing.trace_elf():
            expected += [f"{self.micro_name}_0.elf"]
        if checkpoint:
            expected += [
                f"{self.output_name}_0.chkpt",
                f"{self.output_name}_0.chkpt.lister",
                f"{self.output_name}_0.lister",
                f"{self.output_name}_0.lister0",
                f"{self.output_name}_0.tbt",
                f"{self.output_name}_0.tesquest",
            ]

        self.assertEqual(sorted(self.output_files), sorted(expected))

    def check_micro_trace_stats(self, is_stats):
        self.assertGreaterEqual(is_stats["warmup"], 10600)
        self.assertLessEqual(is_stats["warmup"], 10650)

        self.assertGreaterEqual(is_stats["measurement"], 11050)
        self.assertLessEqual(is_stats["measurement"], 12050)

        self.assertGreaterEqual(is_stats["cooldown"], 4075)
        self.assertLessEqual(is_stats["cooldown"], 5025)

    @unittest.skipUnless(EXEC_TMP, "tmp is not executable")
    def test_tracing_micro(self):
        self.run_micro()
