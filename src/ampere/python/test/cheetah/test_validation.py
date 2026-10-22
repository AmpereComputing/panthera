# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import unittest
from tempfile import TemporaryDirectory

from mocks import generate_study_builder
from panthera import setup_logging
from panthera.cheetah import Args, validate_study

FIXTURE_PATH = os.path.join(os.path.dirname(os.path.dirname(__file__)), "fixtures")


class ValidationTestBase(unittest.TestCase):
    def setUp(self, failures=0, rerun=False):
        setup_logging(True)
        if not rerun:
            self.working_dir = TemporaryDirectory()

        self.study = generate_study_builder(
            "test_study",
            directory=self.working_dir.name,
            jobs=5,
            configs=3,
            failures_per_config=failures,
            rerun=rerun,
        )

    def validate(self, check=False, **kwargs):
        return validate_study(
            self.study,
            self.study._cmds,
            10,
            check=check,
            args=Args(
                run_validate="serial",
                env=False,
                local_jobs=1,
                load=None,
                node_file=None,
                panthera_root=os.path.join(FIXTURE_PATH, "fake_panthera"),
            ).merge(**kwargs),
        )

    def tearDown(self):
        self.working_dir.cleanup()
        setup_logging(False)


class ValiationPassesTest(ValidationTestBase):
    def test_pass(self):
        result = self.validate()
        self.assertEqual(len(list(result.successful_jobs())), 3)
        self.assertEqual(len(list(result.failed_jobs())), 0)


class ValiationFailsTest(ValidationTestBase):
    def setUp(self):
        super().setUp(failures=5)

    def test_fails(self):
        result = self.validate()
        self.assertEqual(len(list(result.successful_jobs())), 0)
        self.assertEqual(len(list(result.failed_jobs())), 3)

    def test_raises_when_fails(self):
        with self.assertRaises(RuntimeError):
            self.validate(check=True)
