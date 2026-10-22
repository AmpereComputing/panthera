# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
from abc import abstractmethod
from argparse import ArgumentParser
from typing import Any

from .job import Job
from .study import StudyBuilder


class Batch:
    """Abstract batcher class."""

    def __init__(
        self,
        study_name: str,
        study_directory: str,
        jobs: list[Job],
        rerun: bool = False,
        file_suffix: str | None = None,
        workdir: str | None = None,
        args: Any = None,
    ):
        self.study_name = study_name
        self.study_directory = study_directory
        self.jobs = jobs
        self.rerun = rerun
        self.args = args

        if file_suffix is None:
            self.file_suffix = ""
        else:
            self.file_suffix = file_suffix if file_suffix.startswith(".") else f".{file_suffix}"

        self.workdir = workdir or self.study_directory

        try:
            self.panthera_install = self.args.panthera_root
        except AttributeError:
            self.panthera_install = os.path.abspath(os.environ["PANTHERA_ROOT"])

    @classmethod
    def from_study(cls, study: StudyBuilder, *, jobs: list[Job] | None = None, **kwargs):
        return cls(
            study.name,
            study.directory,
            jobs or study.run_jobs,
            rerun=study.rerun,
            file_suffix=(f".{study.format_datetime(study.start_time)}" if study.rerun else None),
            **kwargs,
        )

    @property
    def job_count(self) -> int:
        return len(self.jobs)

    def create_job_file(
        self,
        *,
        name: str | None = None,
        jobs: list[Job] | None = None,
        sort_by: str | None = "infer",
    ) -> str:
        name = name or "jobs.txt"
        path = name if os.path.isabs(name) else os.path.join(self.study_directory, name)
        jobs = jobs or self.jobs

        if sort_by == "infer":
            for key in ("order",):
                if all(key in job.meta for job in self.jobs):
                    sort_by = key
                    break

        if sort_by is not None and sort_by != "infer":
            jobs = sorted(jobs, key=lambda j: j.meta[sort_by])

        with open(path, "w") as jf:
            jf.write("\n".join([job.jobfile_relpath for job in jobs]) + "\n")

        return path

    def study_file(self, suffix: str):
        return os.path.join(self.study_directory, self.study_name + self.file_suffix + suffix)

    def work_path(self, path: str):
        return os.path.join(self.workdir, path)

    def panthera_bin(self, tool: str):
        return os.path.join(self.panthera_install, "bin", tool)

    @property
    def run_file(self):
        return os.path.join(self.panthera_install, "bin", "panthera") + " --without-nix run_job --chdir --copy"

    @classmethod
    def parser_add_args(cls, parser: ArgumentParser):
        parser.add_argument(
            "--workdir",
            metavar="DIR",
            help="Working directory, if different than the job list location",
        )

    @abstractmethod
    def setup(self) -> None:
        """Setup any files/collateral for batcher."""

    @abstractmethod
    def run(self) -> bool | None:
        """Run jobs in batcher."""

    def run_results(self) -> dict[str, bool] | None:
        """Results of run (if supported.)"""
        return None
