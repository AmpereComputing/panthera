# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import re

from ..cmd import Cmd


class EVPCmd(Cmd):
    """Basic EVP command."""

    cmd = [
        "{evpexe}",
        "--es", "{event_stream}",
        "-i", Cmd.if_set("interval", "{interval}", "{default_interval}"),
        Cmd.if_set("start", ["--start", "{start}"]),
        Cmd.if_set("stop", ["--stop", "{stop}"]),
        Cmd.if_set("config", "@{config}"),
        "{args}"
    ]  # fmt: skip
    event_stream = "{name}.es.xz"
    evpexe = "{panthera_root}/bin/evp"
    default_interval = "*.gpc.instruction_commit=30000"

    @property
    def validate_per(self) -> str:
        """Granularity to validate for commands."""
        return "directory"

    @classmethod
    def _remove_start(cls, cmd):
        """Remove --start from EVP command, usually for validation."""
        return re.sub(r"--start +[^ ]+ ", r" ", cmd + " ")

    def validation_cmd(self, amount: int) -> Cmd:
        """Command used to validate during validation phase."""
        return self.copy(cmd=self._remove_start(self.render()))
