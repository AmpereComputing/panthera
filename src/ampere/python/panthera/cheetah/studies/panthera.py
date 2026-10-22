# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
from argparse import ArgumentParser

from ..cmd import Cmd


class PantheraCmd(Cmd):
    """Basic panthera command."""

    cmd = """{panthera} {panthera_cmd} {cmd_params}"""

    panthera = "{panthera_root}/bin/panthera"
    cmd_params = []

    @classmethod
    def parser_add_args(cls, parser: ArgumentParser, optional=False, **kwargs):
        super().parser_add_args(parser, **kwargs)
        default_panthera_root = os.environ.get("PANTHERA_ROOT") if optional else os.environ["PANTHERA_ROOT"]
        if default_panthera_root is not None:
            default_panthera_root = os.path.abspath(default_panthera_root)
        parser.add_argument(
            "--panthera-root",
            type=os.path.abspath,
            default=default_panthera_root,
            help="Path to Panthera root",
        )


class EventTracing(PantheraCmd):
    """Event tracing."""

    panthera_cmd = "events.tracing"

    event_tracing_config = "summary.cfg"
    event_stream = "{name}.es.xz"
    event_trace = "{name}.json.gz"

    cmd_params = [
        "--compress",
        "--output",
        "{event_trace}",
        Cmd.join_paths("{panthera_root}", "config/events.tracing", "{event_tracing_config}", prefix="@"),
        "{event_stream}",
    ]  # fmt: skip
