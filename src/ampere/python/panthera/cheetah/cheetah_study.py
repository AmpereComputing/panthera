# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
from typing import Any, Dict, List

from panthera.cheetah import Args, validate_study
from panthera.cheetah.job import Job
from panthera.cheetah.studies.cheetah import Cheetah
from panthera.cheetah.study import RollupStudy as _RollupStudy
from panthera.cheetah.study import StudyBuilder as _StudyBuilder


class Study(_StudyBuilder):
    """Backwards-compatible (legacy) cheetah study."""

    def __init__(
        self,
        name: str,
        specifications: List[Dict[str, Any]] | None = None,
        workloads: str | None = None,
        workload_dir: str | None = None,
        study_directory: str | None = None,
        **kwargs,
    ):
        self._args = dict(kwargs)
        self._args["study"] = name
        cmdpool = kwargs.pop("job_tree", None)
        if not cmdpool:
            cmdpool = Cheetah._setup_from_args(
                self._args,
                specifications=specifications,
                workloads=workloads,
                workload_dir=workload_dir,
            )

        study_directory = study_directory or os.path.abspath(name)
        super().__init__(name, cmdpool, directory=study_directory)

    def validate(self, validate, *args):
        try:
            self.setup()
        except FileExistsError:
            pass

        return validate_study(self, self._cmds, validate, check=False, args=Args(**self._args))


class RollupStudy(_RollupStudy):
    """Backwards-compatible (legacy) cheetah rollup."""

    def __init__(
        self,
        name: str,
        specifications: Dict[str, Any],
        workloads: str,
        workload_dir: str | None = None,
        study_directory: str | None = None,
        **kwargs,
    ):
        args = dict(kwargs)
        args["study"] = name
        cmdpool = Cheetah._setup_from_args(
            args,
            specifications=specifications,
            workloads=workloads,
            workload_dir=workload_dir,
        )
        study_directory = study_directory or os.path.abspath(name)
        study_json = {
            "name": name,
            "study_directory": study_directory,
            "cmd_pool": [Job.from_cmds(cmd, study_directory).to_json() for cmd in cmdpool],
        }
        super().__init__(study_json)

    @classmethod
    def from_json(cls, path_or_buf, **kwargs):
        return _RollupStudy.from_json(path_or_buf, **kwargs)
