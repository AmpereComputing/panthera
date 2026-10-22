# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import unittest

from . import EXEC_TMP, TestMicroTracing


class TestMicroTracingStream(TestMicroTracing):
    micro_name = "fpconvert"
    output_name = micro_name
    stream_name = "fpconvert_0.is.xz"
    stats_type = "stream"

    def command(self, micro_path, kernel_path):
        return [
            "--start-online-warmup-after-offline-warmup-instructions", "1",
            "--start-measuring-after-online-warmup-instructions", "1",
            "--cooldown", "1",
            "--execute", micro_path,
            "--kernel", kernel_path,
        ]  # fmt: skip

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
        self.assertGreaterEqual(is_stats["total instruction count"], 40000)
        self.assertLessEqual(is_stats["total instruction count"], 40050)

    @unittest.skipUnless(EXEC_TMP, "tmp is not executable")
    def test_tracing_micro(self):
        self.run_micro()
