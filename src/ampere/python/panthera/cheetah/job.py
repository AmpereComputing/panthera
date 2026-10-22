# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import json
import logging
import os
import shlex
import subprocess
from pathlib import Path
from typing import Any, Dict, List

from ..run_job import RunnableJob
from .cmd import Cmd, CmdSeq
from .logfile import BatchLogParser, LogFile


class Job(RunnableJob):
    EXCLUDED_META = frozenset(
        [
            "job_name",
            "cmd",
            "validate",
            "validate_per",
            "run_validate",
            "dry_run",
            "debug",
            "study_creation_threads",
            "allow_debug",
            "remove",
            "rerun",
            "order",
        ]
    )
    RUN_META = frozenset(
        [
            "panthera_root",
        ]
    )
    RUN_CMDS_META = frozenset(
        [
            "run_in_shell",
            "inputs",
            "outputs",
        ]
    )
    DEBUG_CHECK_CACHE = {}

    def __init__(
        self,
        name: str,
        directory: str,
        study_directory: str,
        cmds=None,
        meta: Dict[str, Any] | None = None,
        cmdseq: CmdSeq | None = None,
        cmds_meta: List[Dict[str, Any]] | None = None,
    ):
        super().__init__(name, directory, study_directory, cmds=cmds, meta=meta, cmds_meta=cmds_meta)
        self._cmdseq = cmdseq

        self.error = None
        self.tick = 0
        self.complete = False
        self.run_metrics = {}

    @classmethod
    def cmd_for_attribute(cls, attr: str, cmds: CmdSeq | Cmd) -> Cmd | None:
        cmds = cmds.cmds if isinstance(cmds, CmdSeq) else [cmds]
        for cmd in cmds:
            if getattr(cmd, attr):
                return cmd

        return None

    @classmethod
    def attribute_from_cmds(cls, attr: str, cmds: CmdSeq | Cmd) -> str | None:
        cmds = cmds.cmds if isinstance(cmds, CmdSeq) else [cmds]
        values = set(cmd.render(getattr(cmd, attr)) for cmd in cmds if getattr(cmd, attr))
        if len(values) > 1:
            raise ValueError(f"Commands do not have consistent value ({values}) for {attr}")

        return list(values)[0] if values else None

    @classmethod
    def name_from_cmds(cls, cmds: CmdSeq | Cmd) -> str:
        cmd = cls.cmd_for_attribute("name", cmds)
        assert cmd is not None
        assert cmd.name is not None
        return cmd.render(cmd.name)

    @classmethod
    def directory_from_cmds(cls, cmds: CmdSeq | Cmd) -> str:
        directory = cls.attribute_from_cmds("directory", cmds)
        assert directory is not None
        return directory

    @classmethod
    def validation_key_from_cmds(cls, cmds: CmdSeq | Cmd) -> str:
        meta = cls.attribute_from_cmds("validate_per", cmds)
        if not meta:
            raise ValueError("Invalid validate_per of '{meta}' specified for commands")

        key = cls.attribute_from_cmds(meta, cmds)
        if not key:
            raise ValueError("Invalid validation key of '{key}' specified for commands")

        return key

    @classmethod
    def _valid_binary_for_command(cls, cmd: list[str]) -> tuple[bool, str]:
        IGNORE_EXE = frozenset(["panthera"])
        for arg in cmd:
            if (
                arg.startswith("-")
                or os.path.basename(arg) in IGNORE_EXE
                or os.path.splitext(arg)[-1]
                or not os.access(arg, os.X_OK)
            ):
                continue

            if arg not in cls.DEBUG_CHECK_CACHE:
                result = subprocess.check_output(["file", arg]).decode()
                cls.DEBUG_CHECK_CACHE[arg] = "with debug_info" not in result

            return cls.DEBUG_CHECK_CACHE[arg], arg

        return True, ""

    @classmethod
    def from_cmds(
        cls,
        cmdseq: CmdSeq,
        study_directory: str,
        validate: int | None = None,
        allow_debug: bool = True,
    ) -> "Job":
        skipped_validation = 0

        def render_cmd(cmd):
            nonlocal skipped_validation
            if validate is not None:
                try:
                    return cmd.validation_cmd(validate).render()
                except (TypeError, ValueError, AttributeError):
                    skipped_validation += 1

            return str(cmd)

        if not cmdseq:
            raise ValueError("No commands for job")

        name = cls.name_from_cmds(cmdseq)
        directory = cls.directory_from_cmds(cmdseq)
        non_empty_cmdseq = cmdseq.trim()

        if not non_empty_cmdseq:
            raise ValueError(f"All commands are empty when creating job '{name}'")

        cmds = [shlex.split(render_cmd(cmd)) for cmd in non_empty_cmdseq]
        cmds_meta = [cmd.rendered_meta() for cmd in non_empty_cmdseq]
        meta = cmds_meta[0]

        if not allow_debug:
            for cmd in cmds:
                valid, exe = cls._valid_binary_for_command(cmd)
                if not valid:
                    raise ValueError(f"Invalid binary {exe} used, has debug info")

        if validate is not None and skipped_validation == len(cmds):
            logging.warning("All commands did not supply a specialized validation command")

        return cls(
            name,
            directory,
            study_directory,
            cmds=cmds,
            meta=meta,
            cmdseq=non_empty_cmdseq,
            cmds_meta=cmds_meta,
        )

    @classmethod
    def _exclude_meta(cls, key: str, value: Any) -> bool:
        return key in cls.EXCLUDED_META or (isinstance(value, str) and not value)

    @classmethod
    def _filter_meta(cls, meta: Dict[str, Any]) -> Dict[str, Any]:
        return {key: value for key, value in meta.items() if not cls._exclude_meta(key, value)}

    @classmethod
    def _filter_cmds_meta(cls, meta: Dict[str, Any], cmds_meta: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
        return [
            {
                key: value
                for key, value in cmd_meta.items()
                if not cls._exclude_meta(key, value) and (key not in meta or value != meta[key])
            }
            for cmd_meta in cmds_meta
        ]

    def to_json(self, *, exclude_internal: bool = True):
        """Return a JSON serializable dictionary of the job attributes."""
        json_obj = {attr: getattr(self, attr) for attr in ["name", "directory", "cmds"]}

        if exclude_internal:
            json_obj["meta"] = self._filter_meta(self.meta)
            filtered_meta = self._filter_cmds_meta(self.meta, self.cmds_meta)
            if any(meta for meta in filtered_meta):
                json_obj["cmds_meta"] = filtered_meta
        else:
            json_obj["meta"] = self.meta
            json_obj["cmds_meta"] = self.cmds_meta

        return json_obj

    def write_job_file(self):
        Path(self.absolute_directory).mkdir(parents=True, exist_ok=True)
        with open(self.jobfile_abspath, "w") as out:
            odict = {
                "name": self.name,
                "directory": self.directory,
                "cmds": self.cmds,
            }
            meta = {k: v for k, v in self.meta.items() if k in self.RUN_META}
            if meta:
                odict["meta"] = meta

            run_cmds_meta = [
                {k: v for k, v in cmd_meta.items() if k in self.RUN_CMDS_META} for cmd_meta in self.cmds_meta
            ]
            if run_cmds_meta:
                odict["cmds_meta"] = run_cmds_meta
            json.dump(odict, out, indent=4)
        return self.jobfile_relpath

    def matches_filter(self, name_filter):
        return bool(name_filter.search(self.name))

    def print(self):
        """Print the commands to stdout."""
        cmdlines = [self._run_cmd(cmd)[1] for cmd in self.cmds]

        print(" && ".join(cmdlines))

    @property
    def failed(self):
        return self.error is not None

    @property
    def successful(self):
        return self.complete and not self.failed

    @property
    def incomplete(self):
        return not self.complete and not self.failed

    def parse_from_log(self, logfile):
        self.complete = logfile.complete
        self.error = logfile.error
        if logfile.tick is not None:
            self.tick = logfile.tick
        self.run_metrics = logfile.run_metrics

    @classmethod
    def parser(cls):
        return LogFile

    def _parse_batcher_logs(self):
        for parser in BatchLogParser.parsers():
            reason = parser().parse(self.log_abspath)
            if reason is not None:
                return reason

        return None

    def parse_log(self):
        log = self.log_relpath
        logging.debug(f"Parsing {log}")
        self.parse_from_log(
            self.parser().from_file(
                self.log_abspath,
                self.timeseries_abspath,
                self.summary_abspath,
            )
        )
        # Try diagnosing incomplete reason
        if not self.complete and not self.error:
            reason = self._parse_batcher_logs()
            if reason is not None:
                self.error = reason

        logging.debug(f"Parsed {log} complete: {self.complete}; error: {self.error}")

    def report(self):
        info = {}
        info["name"] = self.name
        if self.complete:
            info["status"] = "run"
        else:
            info["status"] = "incomplete"
        if self.error:
            info["error"] = {}
            info["error"]["message"] = self.error
        return info
