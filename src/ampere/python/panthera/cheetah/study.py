# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import json
import logging
import os
import shutil
from abc import abstractmethod
from concurrent.futures import ProcessPoolExecutor
from datetime import datetime
from functools import partial
from glob import glob
from io import TextIOWrapper
from pathlib import Path
from typing import Any

from .. import check_nix_env
from .cmd import Cmd, CmdPool, CmdSeq
from .job import Job
from .logfile import LogFile, LogParsing
from .workdir import workdir
from .xml_report import XMLReport


class Study:
    def __init__(self, name: str, directory: str, parsed_logs: LogParsing | bool | None = None):
        self.name = name
        self.directory = os.path.abspath(directory)
        self.run_jobs: list[Job] = []
        if isinstance(parsed_logs, bool):
            self.parsed_logs = LogParsing(parsed=parsed_logs)
        elif parsed_logs is None:
            self.parsed_logs = LogParsing()
        else:
            self.parsed_logs = parsed_logs

    @abstractmethod
    def all_jobs(self):
        """Iterator for all jobs."""

    def unique_meta_values(self, name) -> list[Any]:
        """Get unique meta values"""
        return list(set(job.meta[name] for job in self.all_jobs() if name in job.meta))

    def parse_logs(self) -> None:
        if self.parsed_logs:
            return

        if not self.parsed_logs.parallelized():
            for job in self.all_jobs():
                job.parse_log()
        else:
            jobs = list(self.all_jobs())
            parser = jobs[0].parser()
            with ProcessPoolExecutor(self.parsed_logs.parallel) as pool:
                logs = pool.map(
                    parser.from_file,
                    [job.log_abspath for job in jobs],
                    [job.timeseries_abspath for job in jobs],
                    [job.summary_abspath for job in jobs],
                )

            for job, log in zip(jobs, logs):
                job.parse_from_log(log)

        self.parsed_logs.parsed = True

    def successful_jobs(self):
        self.parse_logs()
        for job in self.all_jobs():
            if job.successful:
                yield job

    def failed_jobs(self):
        self.parse_logs()
        for job in self.all_jobs():
            if job.failed:
                yield job

    def incomplete_jobs(self):
        self.parse_logs()
        for job in self.all_jobs():
            if job.incomplete:
                yield job

    def create_xml_report(self) -> XMLReport:
        return XMLReport.from_study(self)


class RollupStudy(Study):
    """Loads a study for rollup/analysis."""

    def __init__(self, study_json: dict[str, Any], **kwargs):
        self.job_class = kwargs.pop("job_class", Job)
        super().__init__(study_json["name"], study_json.get("study_directory"), **kwargs)
        self.study_json = study_json
        # Only for cheetah and XML reports
        self.parallel_info = study_json.get("parallel_info", None)

        self.jobs = [self.make_job(job) for job in study_json["cmd_pool"]]

    @classmethod
    def from_cheetah_json(cls, study_json: dict[str, Any], study_directory: str | None = None, **kwargs):
        """Load a study from cheetah JSON."""
        if study_directory is not None:
            study_json["study_directory"] = study_directory
        return cls(study_json, **kwargs)

    @classmethod
    def from_legacy_cheetah_json(
        cls,
        study_json: dict[str, Any],
        study_directory: str,
        **kwargs,
    ) -> "RollupStudy":
        """Load a study from legacy cheetah JSON (for backwards compatability)."""
        specifications = study_json.pop("specifications", {})
        _workloads = study_json.pop("workloads", {})
        job_tree = study_json.pop("job_tree", [])
        study_json = {"name": study_json.pop("name")}

        def job_to_cmd(cmd, spec):
            cmd["directory"] = os.path.basename(cmd["directory"])
            if "meta" not in cmd:
                cmd["meta"] = {
                    "configuration": spec["name"],
                }
            return cmd

        study_json["cmd_pool"] = []
        for jobs, spec in zip(job_tree, specifications):
            study_json["cmd_pool"].extend([job_to_cmd(job, spec) for job in jobs])

        study_json["study_directory"] = study_directory
        study_json["job_list"] = os.path.join(study_directory, "jobs.txt")

        return cls(study_json, **kwargs)

    @classmethod
    def from_json(
        cls,
        path_or_buf: str | TextIOWrapper,
        study_directory: str | None = None,
        **kwargs,
    ) -> "RollupStudy":
        if isinstance(path_or_buf, str):
            if os.path.basename(path_or_buf) != "study.json" and os.path.isdir(path_or_buf):
                path_or_buf = os.path.join(path_or_buf, "study.json")

            if study_directory is None:
                study_directory = str(Path(path_or_buf).absolute().parent)

            with open(path_or_buf, "r") as jf:
                study_json = json.load(jf)
        else:
            study_json = json.load(path_or_buf)

        if study_json.get("study_creator") == "cheater":
            return cls.from_cheetah_json(
                study_json,
                study_directory=study_directory,
                **kwargs,
            )

        if study_directory is None:
            raise ValueError("study_directory is required for cheetah studies")

        # For backwards compatibility
        return cls.from_legacy_cheetah_json(study_json, study_directory, **kwargs)

    @classmethod
    def _infer_study_directory(cls, path: str) -> str:
        study_directory = Path(path)
        while not (study_directory / "study.json").is_file():
            study_directory = study_directory.parent
            if len(study_directory.parts) <= 1:
                study_directory = path
                break

        return str(study_directory)

    @classmethod
    def from_directory(cls, path: str, **kwargs) -> "RollupStudy":
        path = os.path.realpath(path)
        study_json = os.path.join(path, "study.json")
        if os.path.isfile(study_json):
            return cls.from_json(study_json, study_directory=path, **kwargs)

        study_directory = cls._infer_study_directory(path)
        files = []
        job_like_suffixes = ("*.json", "*.log")
        for suffix in job_like_suffixes:
            files = list(glob(os.path.join(path, "**", suffix), recursive=True))
            if files:
                break

        return cls.from_files(files, study_directory=study_directory, **kwargs)

    @classmethod
    def from_files(cls, files: list[str], study_directory=None, **kwargs) -> "RollupStudy":
        study_directory = study_directory or cls._infer_study_directory(os.path.dirname(files[0]))

        def job_from_path(job_file):
            job_path = os.path.splitext(job_file)[0]
            job_json = {"name": os.path.basename(job_path), "meta": {}, "cmds": []}

            job_json_path = f"{job_path}.json"
            if os.path.isfile(job_json_path):
                with open(job_json_path, "r") as jf:
                    job_json.update(json.load(jf))

            job_json["directory"] = os.path.relpath(os.path.dirname(job_file), start=study_directory)
            return job_json

        study_json = {
            "name": os.path.basename(study_directory),
            "study_directory": study_directory,
            "cmd_pool": [
                job_from_path(job_file) for job_file in files if os.path.basename(job_file) not in ("study.json",)
            ],
        }
        return cls(study_json, **kwargs)

    def make_job(self, json_job: dict[str, Any]):
        return self.job_class(
            json_job["name"],
            json_job["directory"],
            self.directory,
            cmds=json_job["cmds"],
            meta=json_job["meta"],
            cmds_meta=json_job.get("cmds_meta"),
        )

    def all_jobs(self):
        return iter(self.jobs)


class StudyBuilder:
    """Builds a study."""

    def __init__(
        self,
        name: str,
        cmds: CmdPool | list[Job],
        directory: str | None = None,
        rerun: bool = False,
        extend: bool = False,
        creator: str = "cheater",
    ):
        self.name = name
        self.directory = os.path.abspath(directory or name)
        self.rerun = rerun
        self.extend = extend
        self.start_time = datetime.now()
        self._cmds = cmds
        self._creator = creator

    @property
    def needs_directory_to_exist(self):
        return self.rerun or self.extend

    @property
    def needs_study_creation(self):
        return self.extend or not self.rerun

    @property
    def running_all_jobs(self):
        return not self.extend and not self.rerun

    def setup(self, remove: bool = False):
        if self.needs_directory_to_exist:
            if remove:
                raise ValueError("Cannot rerun/extend while also removing the study directory")
            if not os.path.isdir(self.directory):
                raise FileNotFoundError(f"{self.directory} does not exist, cannot rerun/extend")
        else:
            logging.debug(f"Creating study directory at: {self.directory}")
            self._make_study_directory(remove=remove)

    def build(
        self,
        validate: int | None = None,
        allow_debug: bool = True,
        threads: int = 4,
        process_only: bool = False,
    ) -> list[Job]:
        check_nix_env()

        previous_study = None if self.running_all_jobs else self.as_rollup()
        if self.rerun:
            self.run_jobs = self._create_failure_jobs(rollup=previous_study)

        elif self.needs_study_creation:
            self.run_jobs = self._build_jobs(
                self._cmds,
                validate=validate,
                allow_debug=allow_debug,
                threads=int(threads),
                process_only=process_only,
            )

            if self.extend:
                self.run_jobs = self._exclude_previous_jobs(self.run_jobs, rollup=previous_study)

        return self.run_jobs

    def as_rollup(self) -> RollupStudy:
        with open(self.study_json, "r") as jf:
            return RollupStudy.from_cheetah_json(json.load(jf), study_directory=os.path.dirname(self.study_json))

    def _make_study_directory(self, remove: bool = False):
        if remove:
            try:
                shutil.rmtree(self.directory)
            except FileNotFoundError:
                pass

        try:
            Path(self.directory).mkdir(parents=True, exist_ok=False)
        except FileExistsError as e:
            logging.error(f'study directory "{self.directory}" already exists')
            raise e

    @staticmethod
    def _expand_job(job: Job, write_job_file=True) -> tuple[str, dict[str, Any]]:
        filename = job.write_job_file() if write_job_file else job.jobfile_relpath
        return (filename, job.to_json(exclude_internal=False))

    @staticmethod
    def _expand_job_func(_study_directory: str, job: Job, write_job_file: bool = True, **_kwargs):
        return StudyBuilder._expand_job(job, write_job_file=write_job_file)

    @staticmethod
    def _build_job(
        study_directory: str,
        cmdseq: CmdSeq | Cmd,
        validate: int | None = None,
        allow_debug: bool = True,
        write_job_file: bool = True,
    ):
        if not isinstance(cmdseq, CmdSeq):
            cmdseq = CmdSeq([cmdseq])

        try:
            job = Job.from_cmds(
                cmdseq,
                study_directory,
                validate=validate,
                allow_debug=allow_debug,
            )
            return StudyBuilder._expand_job(job, write_job_file=write_job_file)
        except Exception as e:
            name = Job.name_from_cmds(cmdseq)
            raise ValueError(f"Issue building job: {name}") from e

    def make_filename(self, name: str) -> str:
        """Construct an absolute file name based on the study directory."""
        return os.path.join(self.directory, name)

    @property
    def _rerun_job_list(self):
        return self.make_filename(f"jobs.{self.format_datetime(self.start_time)}.txt")

    @property
    def job_list(self):
        if not self.running_all_jobs:
            return self._rerun_job_list
        return self.make_filename("jobs.txt")

    @classmethod
    def format_datetime(cls, time: datetime):
        return time.strftime("%Y_%m_%d_%H_%M_%S")

    @property
    def study_json(self):
        return self.make_filename("study.json")

    def _check_no_duplicates(self, job_paths: list[str]):
        jobfiles = set(job_paths)
        duplicated_jobs = len(job_paths) - len(jobfiles)
        if duplicated_jobs == 0:
            return

        duplicates = []
        for job in job_paths:
            if job not in jobfiles:
                duplicates.append(job)
            else:
                jobfiles.remove(job)

        duplicate_files = "\n".join(duplicates)
        raise RuntimeError(f"{duplicated_jobs} duplicate jobs\n: {duplicate_files}")

    def _create_failure_jobs(self, rollup: RollupStudy) -> list[Job]:
        """Create subset job list of only failed/incomplete jobs."""
        failed_job_paths = set(job.jobfile_relpath for job in rollup.failed_jobs())
        incomplete_job_paths = set(job.jobfile_relpath for job in rollup.incomplete_jobs())
        rerun_job_paths = failed_job_paths | incomplete_job_paths
        if not rerun_job_paths:
            raise ValueError("No failed or incomplete jobs found")

        logging.debug(f"Found {len(failed_job_paths)} failed and {len(incomplete_job_paths)} incomplete jobs")
        return [job for job in rollup.all_jobs() if job.jobfile_relpath in rerun_job_paths]

    def _exclude_previous_jobs(self, jobs: list[Job], rollup: RollupStudy) -> list[Job]:
        """Exclude old/previous jobs from current job list."""
        old_job_paths = set(job.jobfile_relpath for job in rollup.all_jobs())
        new_jobs = [job for job in jobs if job.jobfile_relpath not in old_job_paths]
        if not new_jobs:
            raise ValueError("No new jobs found")

        logging.debug(f"Found {len(new_jobs)} new jobs")
        return new_jobs

    def _create_study_json(self, jobs_json: list[dict[str, Any]]) -> dict[str, Any]:
        return {
            "name": self.name,
            "study_directory": self.directory,
            "study_creator": self._creator,
            "job_list": self.job_list,
            "cmd_pool": jobs_json,
        }

    def _save_study_json(self, jobs_json: list[dict[str, Any]]):
        with open(self.study_json, "w") as study_json:
            json.dump(
                self._create_study_json(jobs_json),
                study_json,
                indent=4,
            )

    def to_json(self):
        def job_json(cmd_or_job: Cmd | Job) -> dict[str, Any]:
            if not isinstance(cmd_or_job, Job):
                job = Job.from_cmds(cmd_or_job, self.directory)
            else:
                job = cmd_or_job

            return job.to_json()

        return self._create_study_json([job_json(cmd_or_job) for cmd_or_job in self._cmds])

    def _build_jobs(
        self,
        cmds_or_jobs: CmdPool | list[Job],
        validate: int | None = None,
        allow_debug: bool = True,
        threads: int = 4,
        process_only: bool = False,
    ) -> list[Job]:
        job_list = isinstance(cmds_or_jobs, list) and isinstance(cmds_or_jobs[0], Job)
        if not isinstance(cmds_or_jobs, CmdPool) and not job_list:
            raise ValueError(f"Commands are not a CmdPool or list of Jobs, they're a {type(cmds_or_jobs)} instead")
        if len(cmds_or_jobs) == 0:
            raise ValueError("Job-list is empty (no commands provided)")

        job_builder = partial(
            StudyBuilder._expand_job_func if job_list else StudyBuilder._build_job,
            self.directory,
            validate=validate,
            allow_debug=allow_debug,
            write_job_file=not process_only,
        )

        logging.debug(f"Building {len(cmds_or_jobs)} jobs")
        with workdir(self.directory):
            if threads <= 1:
                job_cmds = map(job_builder, cmds_or_jobs)
            else:
                with ProcessPoolExecutor(threads) as pool:
                    job_cmds = pool.map(job_builder, cmds_or_jobs)

            job_cmds = list(job_cmds)
            job_files = [job[0] for job in job_cmds]
            jobs = [Job.from_json(job[1], study_directory=self.directory) for job in job_cmds]
            self._check_no_duplicates(job_files)

            if process_only:
                return jobs

            logging.debug(f"Creating study JSON")
            self._save_study_json([job[1] for job in job_cmds])

            return jobs
