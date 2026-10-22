# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
from itertools import chain

from panthera.cheetah.cmd import CmdPool
from panthera.cheetah.studies.sim import SimCmd
from panthera.cheetah.study import StudyBuilder
from panthera.constants import SUMMARY_CSV_SUFFIX

SIM = os.path.join(
    os.path.dirname(os.path.dirname(__file__)),
    "fixtures",
    "fake_panthera",
    "bin",
    "sim",
)


def generate_study_command(job, config, failure=False, mem_alloc_func=None, **kwargs):
    sim_cmd = {
        "order": job,
        "name": f"job_{job}",
        "directory": f"config_{config}",
        "cmd": (f"{SIM} {job} --summary {{name}}.{SUMMARY_CSV_SUFFIX}" if not failure else "/bin/false"),
    }

    if mem_alloc_func:
        sim_cmd["max_resident_mb"] = {"amperesys": mem_alloc_func(job)}

    return SimCmd(**sim_cmd, **kwargs)


def generate_study_commands(jobs=5, configs=3, failures_per_config=0, mem_alloc_func=None, **kwargs):
    return CmdPool(
        chain.from_iterable(
            [
                generate_study_command(job, config, job < failures_per_config, mem_alloc_func, **kwargs)
                for job in range(jobs)
            ]
            for config in range(configs)
        )
    )


def study_builder(name, cmds, directory=None, **kwargs):
    return StudyBuilder(
        "test_study",
        cmds,
        directory=os.path.join(directory, name) if directory else None,
        **kwargs,
    )


def generate_study_builder(
    name, directory=None, jobs=5, configs=3, failures_per_config=0, mem_alloc_func=None, job_args=None, **kwargs
):
    job_args = job_args or {}
    return study_builder(
        name,
        generate_study_commands(
            jobs=jobs,
            configs=configs,
            failures_per_config=failures_per_config,
            mem_alloc_func=mem_alloc_func,
            **job_args,
        ),
        directory=directory,
        **kwargs,
    )
