# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import logging
import os
from argparse import ArgumentParser

from ..batch import Batch
from ..job import Job
from ..workdir import workdir


class Serial(Batch):
    def __init__(self, study_name: str, study_directory: str, jobs: list[Job], **kwargs):
        super().__init__(study_name, study_directory, jobs, **kwargs)
        self._results = None

    @classmethod
    def parser_add_args(cls, parser: ArgumentParser):
        pass

    def setup(self) -> None:
        pass

    def run(self) -> bool:
        errors = 0
        self._results = {}
        logging.info(f"Running {self.job_count} jobs")
        with workdir(self.study_directory):
            for job in self.jobs:
                self._results[os.path.join(job.directory, job.name)] = success = job.run(chdir=True)
                if not success:
                    errors += 1

        return errors == 0

    def run_results(self) -> dict[str, bool] | None:
        return self._results
