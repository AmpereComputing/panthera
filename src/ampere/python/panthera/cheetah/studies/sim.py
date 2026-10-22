# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import re
from argparse import ArgumentParser

from ..cmd import Cmd
from .evp import EVPCmd
from .panthera import PantheraCmd


class SimCmd(PantheraCmd):
    """Basic simulation command."""

    cmd = """{panthera} sim {sim_params} {simexe} {sim} {evpsep} {evp}"""

    # Absolute paths
    inputs = ["{panthera_root}"]

    # Relative path from study output directory
    outputs = ["{directory}/{name}.summary.csv"]

    simexe = "{panthera_root}/bin/amperesys"
    config = "{panthera_root}/config"
    config_model = "{config}/model"
    evpsep = Cmd.if_set("evp", "--")
    sim = []
    evp = []
    sim_params = []

    @classmethod
    def parser_add_args(cls, parser: ArgumentParser, **kwargs):
        super().parser_add_args(parser, **kwargs)

    @property
    def validate_per(self) -> str:
        """Granularity to validate for commands."""
        return "directory"

    def validation_cmd(self, amount: int) -> Cmd:
        """Command used to validate during validation phase."""
        return self.copy(evp=EVPCmd._remove_start(self.render(self.evp)))


class WorkloadBasedCmd(Cmd):
    """Basic command for workload-based commands."""

    _workload_required = True

    @classmethod
    def parser_add_args(cls, parser: ArgumentParser, **kwargs):
        workloads_default = kwargs.get("workloads_default")
        parser.add_argument(
            "-w",
            "--workloads",
            required=cls._workload_required,
            help="Workload list",
            **(dict(default=workloads_default) if workloads_default else {}),
        )

    @classmethod
    def _expand_workload_directory(cls, workload_path: str) -> str:
        return os.path.dirname(os.path.realpath(workload_path))

    @property
    def workload_root(self) -> str:
        """Directory where workload files (i.e. is.xz) for workloads are stored."""
        return self._expand_workload_directory(self.render("{workloads}"))


class WorkloadSimCmd(SimCmd, WorkloadBasedCmd):
    """Basic simulation command for workload-based simulations."""

    @classmethod
    def parser_add_args(cls, parser: ArgumentParser, **kwargs):
        SimCmd.parser_add_args(parser, **kwargs)
        WorkloadBasedCmd.parser_add_args(parser, **kwargs)


class ArbitraryCmd(WorkloadBasedCmd):
    """Basic workload-based (with optional panthera root) command."""

    cmd = "{command}"
    directory = "."
    run_in_shell = True

    @classmethod
    def parser_add_args(cls, parser: ArgumentParser, **kwargs):
        super().parser_add_args(parser, **kwargs)
        PantheraCmd.parser_add_args(parser, optional=True, **kwargs)
        parser.add_argument(
            "command",
            type=str,
            nargs="+",
            help="String(s) using python format-string syntax to use as a template for generating each job's command(s) to be run in a bash shell",
        )
