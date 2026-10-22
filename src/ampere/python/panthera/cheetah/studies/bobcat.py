# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from argparse import ArgumentParser

from ..cmd import Cmd
from .panthera import PantheraCmd
from .sim import WorkloadBasedCmd, WorkloadSimCmd


class BobcatSim(WorkloadSimCmd):
    """Simulation command for bobcat simulator."""

    simexe = "{bobcat}"
    sim = "{bobcat_args}"
    directory = "{configuration}"

    bobcat = "{panthera_root}/bin/bobcat"
    bobcat_args = []

    def parser_add_args(self, parser: ArgumentParser, **kwargs):
        bobcat_group = parser.add_argument_group(title="Bobcat arguments")
        super().parser_add_args(bobcat_group, **kwargs)

    @classmethod
    def _adjust_args_for_validation(cls, args, amount):
        return args + [
            "-event_stream_always", "true",
            "-top.core.bpu.max_instructions", str(amount),
        ]  # fmt: skip

    def validation_cmd(self, amount: int) -> Cmd:
        return (
            super().validation_cmd(amount).copy(bobcat_args=self._adjust_args_for_validation(self.bobcat_args, amount))
        )


class BobcatRTLWarmup(WorkloadBasedCmd, PantheraCmd):
    """Bobcat RTL csv file generation"""

    bobcat_cache = "bobcat_cache"
    json = "{bobcat_cache}/bobcat_bpu_warmup.json.gz"
    csv = "bpu_warmup.csv"
    core = "connor"
    cmd = "{panthera} snippets.generate_rtl_bpu_warmup --core {core} --panthera-warmup {json} --rtl-warmup-file {csv}"

    @classmethod
    def parser_add_args(cls, parser: ArgumentParser, **kwargs):
        PantheraCmd.parser_add_args(parser, **kwargs)
        WorkloadBasedCmd.parser_add_args(parser, **kwargs)
