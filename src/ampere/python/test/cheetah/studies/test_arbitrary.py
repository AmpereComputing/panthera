# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from panthera.cheetah.cmd import CmdSeq
from panthera.cheetah.job import Job
from panthera.cheetah.studies.sim import ArbitraryCmd

from . import BaseSimTest


class ArbitraryCmdTest(BaseSimTest):
    def render_sim(self, **kwargs):
        return self.render(ArbitraryCmd(**self.settings, **self.trace, **kwargs))

    def cmd_to_job(self, validate: int | None = None, **kwargs):
        return Job.from_cmds(
            CmdSeq([ArbitraryCmd(**self.settings, **self.trace, **kwargs)]),
            study_directory="/somewhere/some_study",
            validate=validate,
        )

    def test_basic(self):
        expected = self.normalize(["./install/bin/is_something ./build/ampere/python/something.is.xz > something.txt"])
        # FIXME: `workload_root` will not always work correctly for absolute `file`
        self.assertEqual(
            self.render_sim(command="./install/bin/is_something {workload_root}/{file} > {name}.txt"),
            expected,
        )

    def test_panthera(self):
        expected = self.normalize(["./install/bin/is_something ./build/ampere/python/something.is.xz > something.txt"])
        # FIXME: `workload_root` will not always work correctly for absolute `file`
        self.assertEqual(
            self.render_sim(command="{panthera_root}/bin/is_something {workload_root}/{file} > {name}.txt"),
            expected,
        )
