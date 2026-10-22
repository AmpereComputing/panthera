# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import unittest
from glob import glob
from tempfile import TemporaryDirectory

from mocks import generate_study_builder
from panthera import setup_logging
from panthera.cheetah import Args
from panthera.cheetah.batchers.serial import Serial

FIXTURE_PATH = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(__file__))), "fixtures")


class SerialBatcherTestBase(unittest.TestCase):
    def setUp(self, failures=0, configs=3, rerun=False, extend=False):
        setup_logging(True)
        if not rerun and not extend:
            self.working_dir = TemporaryDirectory()

        self.study = generate_study_builder(
            "test_study",
            directory=self.working_dir.name,
            jobs=5,
            configs=configs,
            failures_per_config=failures,
            rerun=rerun,
            extend=extend,
        )
        self.study.setup()
        self.study.build()
        self.batcher = Serial.from_study(
            self.study, args=Args(panthera_root=os.path.join(FIXTURE_PATH, "fake_panthera"))
        )

    def tearDown(self):
        self.working_dir.cleanup()
        setup_logging(False)


class SerialBatcherTest(SerialBatcherTestBase):
    def test_setup(self):
        self.batcher.setup()

    def test_run(self):
        self.batcher.setup()
        results = self.batcher.run()
        self.assertTrue(results)

        results = self.batcher.run_results()
        self.assertIsNotNone(results)
        self.assertEqual(len(results), 15)
        self.assertTrue(all(results.values()))


class RerunSerialBatcherTest(SerialBatcherTestBase):
    def setUp(self):
        super().setUp(failures=1)
        self.success = self.batcher.run()
        self.results = self.batcher.run_results()
        self.assertFalse(self.success)
        self.assertIsNotNone(self.results)
        self.assertEqual(len(self.results), 15)
        self.assertTrue(sum(self.results.values()), 12)

        super().setUp(rerun=True)

    def test_rerun(self):
        self.batcher.setup()
        self.batcher.run()

        results = self.batcher.run_results()
        self.assertIsNotNone(results)
        self.assertEqual(len(results), 3)


class ExtendSerialBatcherTest(SerialBatcherTestBase):
    def setUp(self):
        super().setUp()
        super().setUp(configs=4, extend=True)

    def test_extended(self):
        self.batcher.run()

        results = self.batcher.run_results()
        self.assertIsNotNone(results)
        self.assertEqual(len(results), 5)
