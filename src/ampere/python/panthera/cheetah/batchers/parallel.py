# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import logging
import os
import subprocess
import sys
from argparse import ArgumentParser

import pandas as pd

from panthera.constants import SUMMARY_CSV_SUFFIX, TIMESERIES_CSV_SUFFIX

from ..batch import Batch
from ..job import Job


class Parallel(Batch):
    def __init__(self, study_name: str, study_directory: str, jobs: list[Job], **kwargs):
        super().__init__(study_name, study_directory, jobs, **kwargs)

        self.env = getattr(self.args, "env", False)
        self.local_jobs = getattr(self.args, "local_jobs", 1)
        self.load = getattr(self.args, "load", None)
        self.node_file = getattr(self.args, "node_file", None)
        self._results = None

    @classmethod
    def parser_add_args(cls, parser: ArgumentParser):
        parallel_group = parser.add_argument_group("Parallel options")
        parallel_group.add_argument("--load", help="stay below this target load on each server")
        parallel_group.add_argument(
            "--parallel-node-file", dest="node_file", help="file with list of machines to run on"
        )
        parallel_group.add_argument(
            "--local-jobs",
            default=1,
            help="number of simultaneous parallel jobs on the local machine",
        )
        parallel_group.add_argument(
            "--env",
            action="store_true",
            help="replicate environment to remote host if the submitter's and workers' don't match",
        )

    @classmethod
    def parse_joblog(cls, joblog: str) -> pd.DataFrame:
        joblog_header = [
            "JobSeq",
            "Host",
            "Starttime",
            "Runtime",
            "Send",
            "Receive",
            "Exitval",
            "Signal",
            "Command",
        ]
        dataf = pd.read_csv(joblog, names=joblog_header, skiprows=1, sep="\t")
        namef = dataf["Command"].str.rsplit(" ", n=1, expand=True)
        dataf["Name"] = namef[1].str.split(".json", n=1, expand=True)[0]
        dataf.set_index("Name", inplace=True)
        return dataf

    def run_joblist_with_parallel(self, job_list: str, logfile: str, args: list[str]) -> pd.DataFrame | None:
        parallel = ["parallel", "--joblog", logfile] + args + ["-a", job_list, self.run_file]
        logging.debug(" ".join(parallel))
        subprocess.run(parallel, cwd=self.study_directory)
        if os.path.exists(logfile):
            return self.parse_joblog(logfile)
        return None

    def setup(self) -> None:
        self.job_list = self.create_job_file()

    def run(self) -> bool:
        parallel_study_name = self.study_file(".joblog")
        logging.debug(f"Creating joblog file {parallel_study_name}")
        parallel_opts = []
        if self.load is not None:
            parallel_opts += ["--load", self.load]
        if self.node_file is not None:
            parallel_opts += [
                "--workdir", self.workdir,
                "--sshdelay", "0.1",
                "--sshloginfile", self.node_file,
                "--retries", "7",
            ]  # fmt: skip
            if self.env:
                parallel_opts += [
                    "--env", "LD_LIBRARY_PATH",
                    "--transferfile", "{.}.json",
                    "--return", f"{{.}}.{TIMESERIES_CSV_SUFFIX}",
                    "--return", f"{{.}}.{SUMMARY_CSV_SUFFIX}",
                    "--return", "{.}.log",
                    "--retries", "5",
                    "--halt", "now,fail=1",
                ]  # fmt: skip
        else:
            parallel_opts += ["--jobs", str(self.local_jobs)]

        if sys.stdout.isatty() and (self.node_file is None or not self.env):
            parallel_opts += ["--eta"]

        logging.debug("Running jobs in parallel")
        self._results = self.run_joblist_with_parallel(self.job_list, parallel_study_name, parallel_opts)

        return (self._results["Exitval"] == 0).all()

    def run_results(self) -> dict[str, bool] | None:
        if self._results is None:
            return None

        return {index: row["Exitval"] == 0 for index, row in self._results.iterrows()}
