# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os

from panthera.cheetah.job import Job
from panthera.cheetah.studies.cheetah import Cheetah, CheetahSpecification
from panthera.constants import SUMMARY_CSV_SUFFIX, TIMESERIES_CSV_SUFFIX

from . import ROOTDIR, BaseSimTest


class CheetahTest(BaseSimTest):
    def setUp(self):
        super().setUp()
        self.trace = Cheetah._expand_trace(self.trace, workload_dir="./build/src/ampere")
        self.spec = {
            "name": "test",
            "exe": "{panthera_root}/bin/amperesys",
            "parameters": [
                "-cfg", "{panthera_root}/config/model/sys/rogue.cfg",
                "-sys.soc_0.core_0.instruction_stream_name", "{workload_path}",
                "-sys.soc_0.core_0.gpc.max_instructions", "10000000",
            ],
        }  # fmt: skip

    def test_expand_with_default_traces(self):
        cmds = Cheetah._expand_specification(self.spec, workload_dir="./build/src/ampere", default_traces=[self.trace])
        self.assertEqual(len(cmds), 1)

    def test_expand_without_default_raises(self):
        with self.assertRaises(ValueError, msg="--workloads not provided and no 'workloads' provided for 'test'"):
            Cheetah._expand_specification(self.spec, workload_dir="./build/src/ampere")


class CheetahSimTest(BaseSimTest):
    def setUp(self):
        super().setUp()
        self.trace = Cheetah._expand_trace(self.trace, workload_dir="./build/src/ampere")
        self.spec = {
            "name": "test",
            "exe": "{panthera_root}/bin/amperesys",
            "parameters": [
                "-cfg", "{panthera_root}/config/model/sys/rogue.cfg",
                "-sys.soc_0.core_0.instruction_stream_name", "{workload_path}",
                "-sys.soc_0.core_0.gpc.max_instructions", "10000000",
            ],
        }  # fmt: skip

    def to_cmds(self, specification):
        Cheetah._remap_specification(specification)
        return CheetahSpecification.from_specification(specification)

    def to_job(self, specification, validate: int | None = None):
        return Job.from_cmds(
            self.to_cmds(specification).copy(**self.settings, **self.trace),
            study_directory="/somewhere/some_study",
            validate=validate,
        )

    def render_cmd(self, cmd, **kwargs):
        return self.render(cmd.copy(**self.settings, **self.trace, **kwargs))

    def render_sim(self, specification, **kwargs):
        cmd = self.to_cmds(specification).cmds[0]
        return self.render(cmd.copy(**self.settings, **self.trace, **kwargs))

    def test_defaults(self):
        expected = self.normalize(
            [
                "./install/bin/panthera sim ./install/bin/amperesys",
                "-cfg ./install/config/model/sys/rogue.cfg",
                "-sys.soc_0.core_0.instruction_stream_name ./build/src/ampere/something.is.xz",
                "-sys.soc_0.core_0.gpc.max_instructions 10000000",
                "-- -i *.gpc.instruction_commit=30000 @base.cfg",
                f"+summarize --timeseries something.{TIMESERIES_CSV_SUFFIX} --summary something.{SUMMARY_CSV_SUFFIX}",
            ]
        )
        self.assertEqual(self.render_sim(self.spec), expected)

    def test_evp(self):
        expected = self.normalize(
            [
                "./install/bin/panthera sim ./install/bin/amperesys",
                "-cfg ./install/config/model/sys/rogue.cfg",
                "-sys.soc_0.core_0.instruction_stream_name ./build/src/ampere/something.is.xz",
                "-sys.soc_0.core_0.gpc.max_instructions 10000000",
                "-- -i 10us @other.cfg +capture something.es",
                f"+summarize --timeseries something.{TIMESERIES_CSV_SUFFIX} --summary something.{SUMMARY_CSV_SUFFIX}",
            ]
        )
        self.spec["evp"] = "-i 10us @other.cfg +capture {name}.es".split()
        self.assertEqual(self.render_sim(self.spec), expected)

    def test_no_timeseries(self):
        self.assertNotIn(
            f"--timeseries something.{TIMESERIES_CSV_SUFFIX}",
            self.render_sim(self.spec, no_timeseries=True),
        )

    def test_trace_absolute_path(self):
        self.trace["file"] = "/some/where/something.is.xz"
        self.trace = Cheetah._expand_trace(self.trace, workload_dir="./build/src/ampere")
        self.assertIn(
            "-sys.soc_0.core_0.instruction_stream_name /some/where/something.is.xz",
            self.render_sim(self.spec),
        )

    def test_memstream(self):
        self.trace["memstream_path"] = os.path.join(ROOTDIR, "src", "ampere", "memory_stream", "test", "test.ms.xz")
        self.spec["parameters"] = [
            "-cfg", "{panthera_root}/config/model/sys/rogue.cfg",
            "-sys.soc_0.core_0.instruction_stream_name", "{workload_path}",
            "-sys.soc_0.core_0.l2c.warmup_ms", "{memstream_path}",
            "-sys.soc_0.core_0.gpc.max_instructions", "10000000",
        ]  # fmt: skip
        self.assertRegex(self.render_sim(self.spec), r"-sys.soc_0.core_0.l2c.warmup_ms .*?.ms.xz")

    def test_memstream_missing_both_get_removed(self):
        self.spec["parameters"] = [
            "-cfg", "{panthera_root}/config/model/sys/rogue.cfg",
            "-sys.soc_0.core_0.instruction_stream_name", "{workload_path}",
            ["-sys.soc_0.core_0.l2c.warmup_ms", "{memstream_path}"],
            ["-sys.soc_0.core_0.warmup_ms", "{memstream_path}"],
            "-sys.soc_0.core_0.gpc.max_instructions", "10000000",
        ]  # fmt: skip

        cmd = self.render_sim(self.spec)
        self.assertNotRegex(cmd, r"-sys.soc_0.core_0.l2c.warmup_ms .*?.ms.xz")
        self.assertNotRegex(cmd, r"-sys.soc_0.core_0.warmup_ms .*?.ms.xz")

    def test_memstream_missing_not_used_in_command_list_raises(self):
        self.spec["parameters"] = [
            "-cfg", "{panthera_root}/config/model/sys/rogue.cfg",
            "-sys.soc_0.core_0.instruction_stream_name", "{workload_path}",
            ["-sys.soc_0.core_0.warmup_ms", "{memstream_path}"],
            "-sys.soc_0.core_0.l2c.warmup_ms", "{memstream_path}",
            "-sys.soc_0.core_0.gpc.max_instructions", "10000000",
        ]  # fmt: skip

        with self.assertRaises(
            RuntimeError,
            msg=r"'memstream_path' is not used in a command list (for parameter removal).*",
        ):
            self.render_sim(self.spec)

    def test_adjust_cmd(self):
        self.spec["evp"] = "-i 10us @other.cfg +capture {directory}/{name}.es".split()
        self.spec["parameters"] = [
            "-cfg", "{panthera_root}/config/model/sys/rogue.cfg",
            "-sys.soc_0.core_0.gpc.max_instructions", "10000000",
            "-some_output", "{study_name}/{directory}/{name}.txt",
        ]  # fmt: skip

        cmd = self.render_sim(self.spec)
        self.assertRegex(cmd, r"\+capture something\.es")
        self.assertRegex(cmd, r"-some_output something\.txt")

    def test_pre(self):
        self.spec["pre_sim"] = [["/bin/echo", "pre"]]
        cmds = self.to_cmds(self.spec)
        self.assertEqual(len(cmds.cmds), 2)
        self.assertEqual(self.render(cmds.cmds[0]), "/bin/echo pre")

    def test_post(self):
        self.spec["post_sim"] = [["/bin/echo", "post"]]
        cmds = self.to_cmds(self.spec)
        self.assertEqual(len(cmds.cmds), 2)
        self.assertEqual(self.render(cmds.cmds[-1]), "/bin/echo post")

    def test_adjust_cmd_pre_post(self):
        self.spec["pre_sim"] = [["/bin/echo", "{directory}/{name}.pre.txt"]]
        self.spec["post_sim"] = [["/bin/echo", "{directory}/{name}.post.txt"]]
        cmds = self.to_cmds(self.spec)
        self.assertEqual(self.render_cmd(cmds.cmds[0]), "/bin/echo something.pre.txt")
        self.assertEqual(self.render_cmd(cmds.cmds[-1]), "/bin/echo something.post.txt")

    def test_pre_command_list_not_supported(self):
        self.spec["pre_sim"] = [["/bin/echo", "pre", ["-something", "{memstream_path}"]]]
        with self.assertRaises(
            ValueError,
            msg="Nested command list not supported in pre/post",
        ):
            self.to_cmds(self.spec)

    def test_validate(self):
        self.assertIn(
            "-sys.soc_0.core_0.gpc.max_warmup_instructions 10 -sys.soc_0.core_0.gpc.max_instructions 10 -- ",
            self.normalize(self.to_job(self.spec, validate=10).cmds[0]),
        )

    def test_measurement_instructions(self):
        self.trace["measurement_instructions"] = 2332452
        self.assertIn(
            "-sys.soc_0.core_0.gpc.max_instructions 2332452",
            self.render_sim(self.spec),
        )

    def test_measurement_instructions_larger(self):
        self.trace["measurement_instructions"] = 23324520000
        self.assertIn(
            "-sys.soc_0.core_0.gpc.max_instructions 10000000",
            self.render_sim(self.spec),
        )
