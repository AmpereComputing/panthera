# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from panthera.cheetah.cmd import CmdSeq
from panthera.cheetah.job import Job
from panthera.cheetah.studies.core import CoreSim
from panthera.constants import SUMMARY_CSV_SUFFIX, TIMESERIES_CSV_SUFFIX

from . import BaseSimTest


class CoreSimTest(BaseSimTest):
    def render_sim(self, **kwargs):
        return self.render(CoreSim(**self.settings, **self.trace, **kwargs))

    def cmd_to_job(self, validate: int | None = None, **kwargs):
        return Job.from_cmds(
            CmdSeq([CoreSim(**self.settings, **self.trace, **kwargs)]),
            study_directory="/somewhere/some_study",
            validate=validate,
        )

    def test_defaults(self):
        expected = self.normalize(
            [
                "./install/bin/panthera sim ./install/bin/amperesys",
                "-cfg ./install/config/model/sys/rogue.cfg",
                "-sys.soc_0.core_0.instruction_stream_name ./build/ampere/python/something.is.xz",
                "-sys.soc_0.core_0.gpc.max_instructions 10000000",
                "-- -i *.gpc.instruction_commit=30000 @base.cfg",
                f"+summarize --timeseries something.{TIMESERIES_CSV_SUFFIX} --summary something.{SUMMARY_CSV_SUFFIX}",
            ]
        )
        self.assertEqual(self.render_sim(), expected)

    def test_core(self):
        self.assertRegex(
            self.render_sim(core="domino"),
            r"-cfg .*/domino.cfg",
        )

    def test_extra_core_args(self):
        self.assertRegex(
            self.render_sim(extra_core_args=["-xxx", "4", "-yyy", "5"]),
            r"-xxx 4 -yyy 5 .*--",
        )

    def test_no_timeseries(self):
        self.assertNotIn(
            f"--timeseries something.{TIMESERIES_CSV_SUFFIX}",
            self.render_sim(no_timeseries=True),
        )

    def test_trace_absolute_path(self):
        self.trace["file"] = "/some/where/something.is.xz"
        self.assertIn(
            "-sys.soc_0.core_0.instruction_stream_name /some/where/something.is.xz",
            self.render_sim(),
        )

    def test_measurement_instructions(self):
        self.trace["cooldown_instructions"] = 1000
        self.trace["measurement_instructions"] = 2332452
        self.assertIn(
            "-sys.soc_0.core_0.gpc.max_instructions 2332452",
            self.render_sim(),
        )

    def test_measurement_instructions_larger(self):
        self.trace["cooldown_instructions"] = 1000
        self.trace["measurement_instructions"] = 23324520000
        self.assertIn(
            "-sys.soc_0.core_0.gpc.max_instructions 10000000",
            self.render_sim(),
        )

    def test_missing_cooldown_instructions(self):
        self.trace["measurement_instructions"] = 2332452
        with self.assertRaises(RuntimeError):
            self.render_sim()

    def test_validate(self):
        self.assertIn(
            "-sys.soc_0.core_0.gpc.max_warmup_instructions 10 -sys.soc_0.core_0.gpc.max_instructions 10 -- ",
            self.normalize(self.cmd_to_job(validate=10).cmds[0]),
        )
