# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import json
import os
import re
import shlex
from argparse import ArgumentParser
from typing import Any, Dict, List

from panthera.constants import SUMMARY_CSV_SUFFIX, TIMESERIES_CSV_SUFFIX

from ..cmd import Cmd, CmdPool, CmdSeq
from .bobcat import BobcatSim
from .core import CoreSim, trace_list
from .sim import SimCmd, WorkloadSimCmd


class CheetahCmd(Cmd):
    directory = "{configuration}"

    def validation_cmd(self, amount: int) -> Cmd:
        cmd = self.cmd if isinstance(self.cmd, list) else shlex.split(self.cmd)

        # Try inferring the type of command being run
        if cmd and cmd[0].endswith("/bobcat"):
            return self.copy(cmd=shlex.join(BobcatSim._adjust_args_for_validation(cmd, amount)))

        raise AttributeError(self, name="validation_cmd")


class CheetahSim(CoreSim, CheetahCmd):
    """Cheetah simulation command (for backwards compatibility with cheetah JSON specifications.)"""

    _workload_required = False

    class AdjustSimCommands:
        def __init__(self, sim: List[str | List[str]] | str):
            self._sim = sim

        def _adjust_cmd_for_missing_files(
            self,
            cmds: List[str | List[str]] | str,
            missing_filename_attributes: set[str],
        ) -> List[str | List[str]] | str:
            if not isinstance(cmds, list):
                return cmds

            def cmd_as_attribute(cmd: str):
                if cmd.startswith("{") and cmd.endswith("}"):
                    return cmd.lstrip("{").rstrip("}")
                return None

            filtered_cmds = []
            for cmd in cmds:
                if isinstance(cmd, list):
                    attribute = cmd_as_attribute(cmd[-1])
                    if attribute is None:
                        raise ValueError(
                            f"'{cmd[-1]}' used at end of nested command list (for file) is not an attribute"
                        )
                    if attribute in missing_filename_attributes:
                        continue
                    if attribute not in CheetahSpecification.OTHER_TRACE_FILES.values():
                        raise ValueError(
                            f"Unknown file attribute '{attribute}' used at end of nested command list (for file)"
                        )

                elif attribute := cmd_as_attribute(cmd):
                    if attribute in missing_filename_attributes:
                        raise ValueError(
                            f"'{attribute}' is not used in a command list (for parameter removal), will lead to invalid commands for missing files"
                        )

                filtered_cmds.append(cmd)

            return filtered_cmds

        def _adjust_cmd_for_trace_lengths(
            self,
            cmd: Cmd,
            cmds: List[str] | str,
        ) -> List[str] | str:
            if not isinstance(cmds, list):
                return cmds

            for attr, params in CheetahSpecification.TRACE_LENGTH_PARAMS.items():
                attr_value = getattr(cmd, attr)
                if not attr_value:
                    continue

                attr_value = Cmd.as_numeric(attr_value)
                for index, (c, next_c) in enumerate(zip(cmds, cmds[1:])):
                    if not params.match(c):
                        continue

                    try:
                        value = Cmd.as_numeric(cmd.render(next_c))
                        cmds[index + 1] = str(min(attr_value, value))
                    except ValueError:
                        pass

            return cmds

        def __call__(self, cmd: Cmd):
            # Remove parameters and value if (optional) tracefiles are missing
            sim = self._sim
            missing_files = set()
            for trace_attr, cmd_file_attr in CheetahSpecification.OTHER_TRACE_FILES.items():
                name_path = getattr(cmd, trace_attr, None)
                path = getattr(cmd, cmd_file_attr, name_path)
                if not path or not os.path.exists(path):
                    missing_files.add(cmd_file_attr)

            sim = self._adjust_cmd_for_missing_files(sim, missing_files)

            # Normalize (flatten) commands
            sim = CheetahSpecification.flatten_cmds(sim)

            # Adjust simulation run lengths depending on trace lengths
            sim = self._adjust_cmd_for_trace_lengths(cmd, sim)

            return sim

    evp_base_args = [
        "-i", "{interval_trigger}",
        "{evp_cfg_file}",
    ]  # fmt: skip

    evp = [
        "{evp_base_args}",
        "+summarize",
        Cmd.if_set("no_timeseries", [], [
            "--timeseries", f"{{name}}.{TIMESERIES_CSV_SUFFIX}",
        ]),
        "--summary", f"{{name}}.{SUMMARY_CSV_SUFFIX}",
    ]  # fmt: skip

    def __init__(self, **kwargs):
        _sim = kwargs.pop("sim", None)
        if _sim is not None:
            self.sim = CheetahSim.AdjustSimCommands(_sim)
        super().__init__(**kwargs)

    def parser_add_args(self, parser: ArgumentParser, **kwargs):
        super().parser_add_args(parser, **kwargs)
        cheetah_core_group = parser.add_argument_group(title="Cheetah core arguments")
        cheetah_core_group.add_argument(
            "--validate-params",
            metavar="PARAMS",
            help="run one trace for PARAMS for each configuration",
        )
        return parser

    def validation_cmd(self, amount: int) -> Cmd:
        if not self.validate_params:
            return super().validation_cmd(amount)

        return super(CoreSim, self).validation_cmd(amount).copy(sim=self.sim + shlex.split(self.validate_params))


class CheetahSpecification(CmdSeq):
    """Cheetah specification representation as command-sequence."""

    OTHER_TRACE_FILES = {
        "pan_cache_preload_file": "memstream_path",
        "pan_branch_warmup_file": "branchstream_path",
    }

    TRACE_LENGTH_PARAMS = {"measurement_instructions": re.compile(r"^.*?gpc\.max_instructions$")}

    @classmethod
    def adjust_cmd(cls, cmd: str) -> str:
        # Cheetah did not chdir, so specification files may have {directory} or {study_name}
        for name in ("study_name", "directory"):
            cmd = re.sub(r"^(['\"]?){" + name + r"}\/", r"\1", cmd)

        return cmd

    @classmethod
    def adjust_cmds(cls, cmds: List[str] | str) -> List[str] | str:
        if isinstance(cmds, str):
            return cls.adjust_cmd(cmds)
        return [cls.adjust_cmds(cmd) for cmd in cmds]

    @classmethod
    def flatten_cmds(cls, cmds):
        flattened = []
        for cmd in cmds:
            if isinstance(cmd, list):
                flattened.extend(cls.flatten_cmds(cmd))
            else:
                flattened.append(cmd)
        return flattened

    @classmethod
    def from_specification(cls, specification: Dict[str, Any]) -> "CheetahSpecification":
        pre_cmds = specification.pop("pre_sim", [])
        post_cmds = specification.pop("post_sim", [])
        specification = {key: cls.adjust_cmds(value) for key, value in specification.items()}

        def make_cmd(cmds):
            if isinstance(cmds, list):
                for cmd in cmds:
                    if isinstance(cmd, list):
                        raise ValueError("Nested command list not supported in pre/post")

                cmds = list(map(shlex.quote, cls.flatten_cmds(cmds)))
            else:
                cmds = shlex.split(cmds)
            cmds = cls.adjust_cmds(cmds)
            return CheetahCmd(cmd=cmds, **specification)

        return cls(list(map(make_cmd, pre_cmds)) + [CheetahSim(**specification)] + list(map(make_cmd, post_cmds)))

    def parser_add_args(self, parser: ArgumentParser, **kwargs):
        CheetahSim().parser_add_args(parser, **kwargs)


class Cheetah(CmdPool):
    """Cheetah study."""

    def parser_add_args(self, parser: ArgumentParser, **kwargs):
        CheetahSpecification([]).parser_add_args(parser, **kwargs)
        cheetah_group = parser.add_argument_group(title="Cheetah arguments")
        cheetah_group.add_argument(
            "--specification",
            required=True,
            help="specification for what configurations to run",
        )
        return parser

    @classmethod
    def _expand_trace(cls, trace: Dict[str, Any], workload_dir: str) -> Dict[str, Any]:
        workload_path = os.path.join(workload_dir, trace["file"])
        trace["workload_path"] = workload_path
        trace["job_name"] = "{name}"

        for trace_name, spec_name in CheetahSpecification.OTHER_TRACE_FILES.items():
            if trace_name in trace:
                trace[spec_name] = os.path.join(workload_dir, trace[trace_name])

        return trace

    @classmethod
    def _remap_specification(cls, specification: Dict[str, Any]):
        specification["spec_name"] = specification["configuration"] = specification.pop("name")
        spec_to_sim_mapping = {
            "exe": "simexe",
            "parameters": "sim",
            "sim_parameters": "sim_params",
            "evp": "evp_base_args",
        }
        for spec_param, sim_param in spec_to_sim_mapping.items():
            if spec_param in specification:
                specification[sim_param] = specification.pop(spec_param)

    @classmethod
    def _workload_dir(cls, workload_path: str) -> str:
        return WorkloadSimCmd._expand_workload_directory(workload_path)

    @classmethod
    def _expand_specification(
        cls,
        specification: Dict[str, Any],
        workload_dir: str | None = None,
        default_traces: List[Dict[str, Any]] | None = None,
    ):
        cls._remap_specification(specification)
        workloads = specification.get("workloads")
        if workloads:
            traces = trace_list(workloads)
            workload_dir = cls._workload_dir(workloads)
        elif default_traces:
            traces = list(default_traces)
        else:
            raise ValueError(f"--workloads not provided and no 'workloads' provided for '{specification['spec_name']}'")

        cmds = CheetahSpecification.from_specification(specification)
        return cmds.apply([cls._expand_trace(trace, workload_dir) for trace in traces])

    @classmethod
    def _setup_from_args(
        cls,
        args: Dict[str, Any],
        specifications: List[Dict[str, Any]] | None = None,
        workloads: str | None = None,
        workload_dir: str | None = None,
    ):
        default_traces = workloads or (trace_list(args["workloads"]) if "workloads" in args else None)
        default_workload_dir = workload_dir or (cls._workload_dir(args["workloads"]) if "workloads" in args else None)
        if specifications is None:
            with open(args.pop("specification"), "r") as jf:
                specifications = json.load(jf)

        cmds = CmdPool.merge(
            [
                cls._expand_specification(
                    spec,
                    default_traces=default_traces,
                    workload_dir=default_workload_dir,
                )
                for spec in specifications
            ]
        )

        args["study_name"] = args["study"]
        return cmds.apply([args])

    def apply(self, iterable):
        if not self.cmds and len(iterable) == 1:
            return self._setup_from_args(iterable[0])
        return super().apply(iterable)
