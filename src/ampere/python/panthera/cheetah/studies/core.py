# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import argparse
import json
from typing import Any, Dict, List

from panthera.constants import SUMMARY_CSV_SUFFIX, TIMESERIES_CSV_SUFFIX

from ..cmd import Cmd
from .sim import WorkloadSimCmd


class CoreSim(WorkloadSimCmd):
    sim = [
        "-cfg", Cmd.join_paths("{panthera_root}", "config/model/sys", "{core}.cfg"),
        "-sys.soc_0.core_0.instruction_stream_name", Cmd.join_paths("{workload_root}", "{file}"),
        "{cache_preload}",
        "-sys.soc_0.core_0.gpc.max_instructions", "{_run_instructions}",
        "{extra_core_args}",
    ]  # fmt: skip

    evp = [
        "-i", "{interval_trigger}",
        "{evp_cfg_file}",
        "+summarize",
        Cmd.if_set("no_timeseries", [], [
            "--timeseries", f"{{name}}.{TIMESERIES_CSV_SUFFIX}",
        ]),
        "--summary", f"{{name}}.{SUMMARY_CSV_SUFFIX}",
        "{extra_evp_args}",
    ]  # fmt: skip

    directory = "{configuration}"
    core = "rogue"
    default_interval = "*.gpc.instruction_commit=30000"
    evp_cfg_file = "@base.cfg"
    max_instructions = 10000000
    extra_core_args = ""
    extra_evp_args = ""
    cache_preload = Cmd.if_set("pan_cache_preload_file", [
        "-sys.soc_0.warmup_ms", Cmd.join_paths("{workload_root}", "{pan_cache_preload_file}"),
    ])  # fmt: skip

    @property
    def _run_instructions(self) -> str:
        if self.measurement_instructions and not self.cooldown_instructions:
            raise ValueError("'measurement_instructions' was specified, but not 'cooldown_instructions'")
        return Cmd.min_value("measurement_instructions", "max_instructions")(self)

    @property
    def interval_trigger(self) -> str:
        return self.interval or self.default_interval

    def parser_add_args(self, parser: argparse.ArgumentParser, **kwargs):
        core_group = parser.add_argument_group(title="Core (amperesys) arguments")
        super().parser_add_args(core_group, **kwargs)

        core_group.add_argument(
            "--evp-cfg-file",
            default="@base.cfg",
            help="Event processor config file (default: @base.cfg)",
        )
        core_group.add_argument(
            "-i",
            "--interval",
            help=f"Time series sampling interval (default: {self.default_interval})",
        )
        core_group.add_argument(
            "--no-timeseries",
            action="store_true",
            help="Don't generate a timeseries (i.e. .ts.csv) file",
        )

    def validation_cmd(self, amount: int) -> Cmd:
        adjust_amount = [
            "-event_stream_always",
            "true",
            "-sys.soc_0.core_0.gpc.max_warmup_instructions",
            str(amount),
            "-sys.soc_0.core_0.gpc.max_instructions",
            str(amount),
        ]
        adjusted = super().validation_cmd(amount)
        sim = adjusted.sim if isinstance(adjusted.sim, list) else [adjusted.sim]
        return adjusted.copy(sim=sim + adjust_amount)


class CoreRTLSim(CoreSim):
    """
    Similar to CoreSim, but does not perform SoC-level warm-start from the
    memory stream in the trace list and sets the event stream processor default
    interval to 100 instructions instead of 30,000
    """

    cache_preload = Cmd.if_set("pan_cache_preload_file", [
        "-sys.soc_0.core_0.l2c.warmup_ms", Cmd.join_paths("{workload_root}", "{pan_cache_preload_file}")
    ])  # fmt: skip
    default_interval = "*.gpc.instruction_commit=100"


def trace_list(workloads) -> List[Dict[str, Any]]:
    with open(workloads, "r") as jf:
        return [{**trace, "order": index} for index, trace in enumerate(json.load(jf))]
