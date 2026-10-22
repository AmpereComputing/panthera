# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import re
from abc import abstractmethod
from typing import Any, Dict


class LogParsing:
    """Log parsing settings."""

    def __init__(self, parsed: bool = False, parallel: int | None = None):
        self.parsed = parsed
        self.parallel = parallel

    def __bool__(self):
        return self.parsed

    def parallelized(self) -> bool:
        return self.parallel is not None and self.parallel > 1


class LogFile:
    error_re = re.compile(r"^((Command )?Error.*)|^(Fatal.*)", re.I)
    tick_re = re.compile(r".*clock.sc_tick \@ (\d+) ([munp]?s)?")
    started_re = re.compile(r"Info:( .*?:)? End of elaboration. Starting simulation.")
    stopped_re = re.compile(r".*Simulation stopped by user.*|^Simulation complete: stopped by event stream processor.*")
    successful_exit_re = re.compile(r".*All commands exited successfully.*")
    completion_metrics_re = re.compile(
        r".*completed +: +(?P<total_cycles>[\d,]+) cycles, +(?P<total_instructions>[\d,]+) instructions total, in (?P<cputime>[\d,]+) cpu sec \((?P<walltime>[\d,]+) wall sec\)"
    )
    phase_metrics_re = re.compile(
        r" +Simulator (?P<phase>\w+) +: +(?P<cycles>[\d,]+) cycles, +(?P<instructions>[\d,]+) instructions, +(?P<total_ipc>[\d\.,]+) IPC,.*? (?P<kips>[\d,\.]+) kips, (?P<memory>[\d,\.]+) MB"
    )

    def __init__(self, complete: bool = True):
        self.complete = complete
        self.error = None
        self.tick = None
        self.run_metrics = {}

    @classmethod
    def _convert_metrics(cls, metrics: Dict[str, Any]):
        for metric, value in metrics.items():
            for converter in (int, float):
                try:
                    metrics[metric] = converter(value)
                    break
                except ValueError:
                    continue

    @classmethod
    def _normalize_tick(cls, value: int, units: str | None = None):
        conversion = {"s": 1e12, "ms": 1e9, "us": 1e6, "ns": 1e3}
        return value * conversion.get((units or "ps").lower(), 1)

    def parse(self, log_path: str, *any_required_files: str):
        started = False
        stopped = False
        successful_exit = False
        with open(log_path, errors="ignore") as logfile:
            for line in logfile:
                error = self.error_re.match(line)
                if error and not self.error:
                    self.error = error.group(0)
                tick = self.tick_re.match(line)
                if tick:
                    self.tick = self._normalize_tick(int(tick.group(1)), tick.group(2))
                if self.started_re.match(line):
                    started = True
                elif self.stopped_re.match(line):
                    stopped = True
                elif self.successful_exit_re.match(line):
                    successful_exit = True
                completion_metrics = self.completion_metrics_re.match(line)
                if completion_metrics:
                    self.run_metrics.update(completion_metrics.groupdict())
                phase_metrics = self.phase_metrics_re.match(line)
                if phase_metrics:
                    metrics = phase_metrics.groupdict()
                    phase = metrics.pop("phase")
                    self.run_metrics.update({f"{phase}_{metric}": value for metric, value in metrics.items()})

        if any_required_files:
            files_present = any(os.path.isfile(path) and os.path.getsize(path) > 0 for path in any_required_files)
        else:
            files_present = True

        if started == stopped and successful_exit and files_present:
            self.complete = True

        self._convert_metrics(self.run_metrics)

    @classmethod
    def from_file(cls, log_path: str, *any_required_files: str) -> "LogFile":
        """Parse log file

        Args:
            log_path (str): Log file path
            any_required_files (str): requires at least one of these files to exist (optional)
        """
        log = cls(False)
        if os.path.isfile(log_path):
            log.parse(log_path, *any_required_files)

        return log


class BatchLogParser:
    """Batch log parser (for incomplete logs)."""

    @abstractmethod
    def parse(self, log_path: str) -> str | None:
        pass

    @classmethod
    def parsers(cls):
        return [SlurmLogParser]


class SlurmLogParser(BatchLogParser):
    def parse(self, log_path: str) -> str | None:
        slurm_log = os.path.splitext(log_path)[0] + ".slurm.log"
        errors = []
        try:
            with open(slurm_log, "r") as f:
                for line in f:
                    if "error:" in line and "TMPDIR" not in line:
                        errors.append(line.strip())

        except FileNotFoundError:
            pass

        return errors[0] if errors else None
