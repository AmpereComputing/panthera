# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import logging
from argparse import ArgumentParser

from ..batch import Batch
from ..job import Job
from ..workdir import workdir


class Printer(Batch):
    def __init__(self, study_name: str, study_directory: str, jobs: list[Job], **kwargs):
        super().__init__(study_name, study_directory, jobs, **kwargs)
        self._results = None

    @classmethod
    def parser_add_args(cls, parser: ArgumentParser):
        pass

    def setup(self) -> None:
        pass

    def run(self) -> bool:
        self._results = {}
        logging.info(f"Printing {self.job_count} jobs")
        with workdir(self.study_directory):
            for job in self.jobs:
                job.print()

        return True
