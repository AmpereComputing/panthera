# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import unittest
from glob import glob
from tempfile import TemporaryDirectory

from mocks import generate_study_builder, generate_study_command, generate_study_commands, study_builder
from panthera.cheetah.cmd import CmdPool


class BasicStudyBuilderTest(unittest.TestCase):
    def setUp(self, study=True):
        self.working_dir = TemporaryDirectory()
        if study:
            self.study = generate_study_builder("test_study", directory=self.working_dir.name, jobs=5, configs=3)

    def tearDown(self):
        self.working_dir.cleanup()


class TestStudyBuilderSetup(BasicStudyBuilderTest):
    def test_setup(self):
        self.study.setup()
        self.assertTrue(os.path.isdir(self.study.directory))

    def test_setup_raises_if_present(self):
        self.study.setup()
        with self.assertRaises(FileExistsError):
            self.study.setup()

    def test_setup_with_removal(self):
        self.study.setup()
        self.study.setup(remove=True)

    def test_setup_with_removal_and_not_present(self):
        self.study.setup(remove=True)


class TestStudyBuilderDirectoryStructure(BasicStudyBuilderTest):
    def setUp(self):
        super().setUp()
        self.study.setup()

    def test_build(self):
        self.study.build()
        self.assertEqual(
            set(os.listdir(self.study.directory)),
            {
                "study.json",
                "config_0",
                "config_1",
                "config_2",
            },
        )

        self.assertEqual(len(glob(os.path.join(self.study.directory, "config_*", "*.json"))), 15)


class TestBuildAndRollupIncompleteStudy(BasicStudyBuilderTest):
    def setUp(self):
        super().setUp()
        self.study.setup()
        self.study.build()

    def test_rollup(self):
        rollup_study = self.study.as_rollup()
        self.assertEqual(rollup_study.name, "test_study")
        self.assertEqual(rollup_study.directory, self.study.directory)
        self.assertEqual(sum(1 for _job in rollup_study.incomplete_jobs()), 15)


class TestBuildWithDuplicates(BasicStudyBuilderTest):
    def setUp(self):
        super().setUp(study=False)
        self.cmds = generate_study_commands()

    def make_study(self):
        self.study = study_builder("test_study", self.cmds, directory=self.working_dir.name)
        self.study.setup()

    def test_duplicate_single_job(self):
        self.cmds = self.cmds.concat(CmdPool([generate_study_command(2, 1)]))
        self.make_study()

        with self.assertRaises(RuntimeError):
            self.study.build()

    def test_duplicate_directory(self):
        self.cmds = self.cmds.concat(generate_study_commands(5, 1))
        self.make_study()

        with self.assertRaises(RuntimeError):
            self.study.build()
