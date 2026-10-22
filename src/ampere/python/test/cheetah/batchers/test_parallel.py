# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import unittest
from glob import glob
from tempfile import TemporaryDirectory

from mocks import generate_study_builder
from panthera import setup_logging
from panthera.cheetah import Args
from panthera.cheetah.batchers.parallel import Parallel

FIXTURE_PATH = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(__file__))), "fixtures")


class ParallelBatcherTestBase(unittest.TestCase):
    def setUp(self, failures=0, configs=3, rerun=False, extend=False, **kwargs):
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
        self.batcher = Parallel.from_study(
            self.study,
            args=Args(
                env=False,
                local_jobs=1,
                load=None,
                node_file=None,
                panthera_root=os.path.join(FIXTURE_PATH, "fake_panthera"),
            ).merge(**kwargs),
        )
        self.batcher.setup()

    def tearDown(self):
        self.working_dir.cleanup()
        setup_logging(False)

    def study_files(self, *parts):
        return list(glob(os.path.join(self.study.directory, *parts)))

    def joblog_files(self):
        return self.study_files("*.joblog")


class ParallelBatcherTest(ParallelBatcherTestBase):
    def test_setup(self):
        jobs_file = self.study_files("jobs.txt")
        self.assertTrue(jobs_file)

        with open(jobs_file[0]) as jf:
            jobs = jf.read().splitlines()

        self.assertEqual(jobs[0], "config_0/job_0.json")
        self.assertEqual(jobs[1], "config_1/job_0.json")
        self.assertEqual(jobs[2], "config_2/job_0.json")
        self.assertEqual(jobs[3], "config_0/job_1.json")

    def test_run(self):
        success = self.batcher.run()
        results = self.batcher.run_results()

        self.assertTrue(success)
        self.assertIsNotNone(results)
        self.assertEqual(len(results), 15)
        self.assertTrue(all(results.values()))

        self.assertEqual(len(self.joblog_files()), 1)

        self.assertEqual(len(self.study_files("config*")), 3)
        self.assertEqual(len(self.study_files("config*", "*.log")), 15)


class RerunParallelBatcherTest(ParallelBatcherTestBase):
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
        self.batcher.run()

        results = self.batcher.run_results()
        self.assertIsNotNone(results)
        self.assertEqual(len(results), 3)


class ExtendParallelBatcherTest(ParallelBatcherTestBase):
    def setUp(self):
        super().setUp()
        super().setUp(configs=4, extend=True)

    def test_extended(self):
        self.batcher.run()

        results = self.batcher.run_results()
        self.assertIsNotNone(results)
        self.assertEqual(len(results), 5)
