# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import unittest
from os.path import dirname
from tempfile import TemporaryDirectory

from panthera import install_dir
from panthera.cheetah.cheetah_study import RollupStudy, Study


class TestStudyBase(unittest.TestCase):
    def setUp(self):
        self.specs = [
            dict(name="echoone", exe="/bin/echo", parameters=["one"]),
            dict(name="echotwo", exe="/bin/echo", parameters=["two"]),
        ]
        self.workloads = [
            dict(name="999.hello", file="999.hello.is.xz", suite="greetings", weight=1.0, workload="hello"),
            dict(name="999.goodbye", file="999.goodbye.is.xz", weight=1.0, suite="greetings", workload="goodbye"),
        ]


class TestStudy(TestStudyBase):
    def test_create_study(self):
        s = RollupStudy("test", self.specs, self.workloads, ".")
        self.assertEqual("test", s.name)
        jobs = list(s.all_jobs())
        self.assertEqual(4, len(jobs))
        self.assertEqual("999.hello", jobs[0].name)

    def test_to_json(self):
        s = Study("test", self.specs, self.workloads, ".")
        j = s.to_json()
        self.assertEqual("test", j["name"])
        self.assertEqual("999.goodbye", j["cmd_pool"][1]["name"])

    def test_from_json(self):
        json_filename = os.path.join(dirname(__file__), "test.json")
        s = RollupStudy.from_json(json_filename)
        jobs = list(s.all_jobs())
        self.assertEqual("test", s.name)
        self.assertEqual("999.hello", jobs[0].name)

    def test_create_xml(self):
        s = RollupStudy("test", self.specs, self.workloads, ".")
        jobs = list(s.all_jobs())
        jobs[3].error = "Error: failed"
        report = s.create_xml_report()
        error = report.tree.find(".//failure")
        self.assertEqual("Error: failed", error.get("message"))

    def test_interpolate_root(self):
        spec = [dict(name="echoone", exe="{panthera_root}/foo", parameters="one")]
        s = RollupStudy("test", spec, self.workloads, ".", panthera_root="/opt/panthera")
        jobs = list(s.all_jobs())
        path_to_foo = "/opt/panthera/foo"
        self.assertTrue(path_to_foo in jobs[0].cmds[0])

    def test_empty_parameters(self):
        spec = [dict(name="echoone", exe="{panthera_root}/foo")]
        RollupStudy("test", spec, self.workloads, ".")


class TestStudyValidation(TestStudyBase):
    FAKE_PANTHERA_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../fixtures/fake_panthera"))

    def setUp(self):
        super().setUp()
        self.working_dir = TemporaryDirectory()
        self.cwd = os.getcwd()
        os.chdir(self.working_dir.name)

    def tearDown(self):
        os.chdir(self.cwd)
        self.working_dir.cleanup()

    def test_validate_pass(self):
        s = Study("test_study", self.specs, self.workloads, ".", panthera_root=self.FAKE_PANTHERA_ROOT)
        validation = s.validate(10, install_dir)
        self.assertEqual(len(list(validation.successful_jobs())), 2)
        self.assertEqual(len(list(validation.failed_jobs())), 0)

    def test_validate_fail(self):
        specs = [
            dict(name="one", exe="/bin/echo", parameters=["one"]),
            dict(name="two", exe="/bin/echo", parameters=["--missing-option"]),
        ]
        s = Study("test_study", specs, self.workloads, ".", panthera_root=self.FAKE_PANTHERA_ROOT)
        validation = s.validate(10, install_dir)
        failed = list(job.log_abspath for job in validation.failed_jobs())

        self.assertEqual(len(list(validation.successful_jobs())), 1)
        self.assertEqual(len(failed), 1)
        self.assertTrue(failed[0].endswith("test_study/_validate/two/999.goodbye.log"))
