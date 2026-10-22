# Copyright (c) 2025-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import re
import unittest
from glob import glob
from tempfile import TemporaryDirectory
from unittest import mock

from mocks import generate_study_builder
from panthera import setup_logging
from panthera.cheetah import Args
from panthera.cheetah.batchers.slurm import Slurm

FIXTURE_PATH = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(__file__))), "fixtures")
FAKE_PANTHERA = os.path.join(FIXTURE_PATH, "fake_panthera")
FAKE_PANTHERA_BIN = os.path.join(FAKE_PANTHERA, "bin")


class SlurmBatcherTestBase(unittest.TestCase):
    def setUp(self, jobs=5, failures=0, configs=3, rerun=False, extend=False, mem_alloc_func=None, **kwargs):
        setup_logging(True)
        if not rerun and not extend:
            self.working_dir = TemporaryDirectory()

        self.study = generate_study_builder(
            "test_study",
            directory=self.working_dir.name,
            jobs=jobs,
            configs=configs,
            failures_per_config=failures,
            mem_alloc_func=mem_alloc_func,
            rerun=rerun,
            extend=extend,
        )
        self.study.setup()
        self.study.build()

        self.patch_path = mock.patch.dict(os.environ, {"PATH": f"{FAKE_PANTHERA_BIN}:{os.environ['PATH']}"})
        self.patch_path.start()
        self.batcher = Slurm.from_study(
            self.study, args=Args(slurm_max_jobs=100, panthera_root=FAKE_PANTHERA, **kwargs)
        )
        self.batcher.setup()

    def tearDown(self):
        setup_logging(False)
        self.patch_path.stop()

    def study_file(self, *parts):
        return os.path.join(self.study.directory, *parts)

    def study_files(self, *parts):
        return list(glob(self.study_file(*parts)))

    def verify_links(self, files):
        for name in files:
            if os.path.islink(name):
                link_path = os.path.join(os.path.dirname(name), os.readlink(name))
                self.assertTrue(os.path.exists(link_path))

    def assert_file_matches(self, path, regex):
        with open(path, "r") as f:
            self.assertRegex(f.read(), regex)

    def assert_file_has(self, path, *values):
        with open(path, "r") as f:
            content = f.read()
            for value in values:
                self.assertIn(value, content)


class SlurmBatcherTest(SlurmBatcherTestBase):
    def test_setup(self):
        self.batcher.setup()
        self.assertTrue(set(os.listdir(self.study.directory)).issuperset({"test_study_0.sbatch"}))

    def test_run(self):
        self.batcher.setup()
        self.batcher.run()

        self.assertEqual(len(self.study_files("config*")), 3)

        # 30 logs from directory, 15 of them are slurm logs
        self.assertEqual(len(self.study_files("config*", "*.log")), 30)
        self.assertEqual(len(self.study_files("config*", "*.slurm.log")), 15)


class SlurmBatcherArgumentsTest(SlurmBatcherTestBase):
    def setUp(self):
        super().setUp(
            slurm_account="test-account",
            slurm_partition="test-partition",
            slurm_qos="test-qos",
            slurm_timeout=30,
        )

    def test_setup(self):
        self.assert_file_has(
            self.study_file("test_study_0.sbatch"),
            "--account=test-account",
            "--partition=test-partition",
            "--qos=test-qos",
            "--time=30",
        )


class SlurmBatcherDefaultArgumentsTest(SlurmBatcherTestBase):
    def test_setup(self):
        with open(self.study_file("test_study_0.sbatch"), "r") as sbatch_file:
            sbatch = sbatch_file.read()

        self.assertNotIn("#SBATCH --account", sbatch)
        self.assertNotIn("#SBATCH --partition", sbatch)
        self.assertNotIn("#SBATCH --qos", sbatch)


class RerunSlurmBatcherTest(SlurmBatcherTestBase):
    def setUp(self):
        super().setUp()
        self.batcher.setup()

        super().setUp(rerun=True)

    def test_setup(self):
        self.batcher.setup()
        sbatch_files = list(glob(os.path.join(self.study.directory, "*.sbatch")))
        self.assertEqual(len(sbatch_files), 2)

        rerun_sbatch = [sf for sf in sbatch_files if not sf.endswith("test_study_0.sbatch")][0]
        self.assert_file_matches(rerun_sbatch, re.compile(r"jobs_0.txt"))

        with open(self.batcher.slurm_jobs_txt(0), "r") as jobs:
            jobs = list(jobs.readlines())

        self.assertEqual(len(jobs), 15)

        self.batcher.run()


class RerunFailuresSlurmBatcherTest(SlurmBatcherTestBase):
    def setUp(self):
        super().setUp(failures=1)
        self.batcher.setup()
        self.batcher.run()

        super().setUp(rerun=True)

    def test_rerun(self):
        self.batcher.setup()

        with open(self.batcher.slurm_jobs_txt(0), "r") as jobs:
            jobs = list(jobs.readlines())

        self.assertEqual(len(jobs), 3)

        self.batcher.run()


class ExtendSlurmBatcherTest(SlurmBatcherTestBase):
    def setUp(self):
        super().setUp()
        super().setUp(configs=4, extend=True)

    def test_extend(self):
        with open(self.batcher.slurm_jobs_txt(0), "r") as f:
            self.assertEqual(len(f.readlines()), 5)


class SlurmMultiJobBatcherTest(SlurmBatcherTestBase):
    def setUp(self):
        def mem_func(j):
            if j < 5:
                return 100
            elif j < 20:
                return 400 + j
            else:
                return 100 * j

        super().setUp(jobs=50, mem_alloc_func=mem_func)

    def test_setup(self):
        self.batcher.setup()
        self.assertTrue(
            set(os.listdir(self.study.directory)).issuperset(
                {
                    "test_study_0.sbatch",
                    "test_study_1.sbatch",
                    "test_study_2.sbatch",
                    "test_study_3.sbatch",
                    "test_study_4.sbatch",
                    "jobs_0.txt",
                    "jobs_1.txt",
                    "jobs_2.txt",
                    "jobs_3.txt",
                    "jobs_4.txt",
                }
            )
        )

        with open(self.batcher.slurm_jobs_txt(0), "r") as jobs:
            jobs = jobs.read().splitlines()

        self.assertEqual(jobs[0], "config_0/job_0.json")
        self.assertEqual(jobs[1], "config_1/job_0.json")
        self.assertEqual(jobs[2], "config_2/job_0.json")
        self.assertEqual(jobs[3], "config_0/job_1.json")

    def test_run(self):
        self.batcher.setup()
        self.batcher.run()

        self.assertEqual(len(self.study_files("config*")), 3)

        # 300 logs from directory, 150 of them are slurm logs
        self.assertEqual(len(self.study_files("config_*", "*.log")), 300)
        self.assertEqual(len(self.study_files("config*", "*.slurm.log")), 150)


class SlurmMultiJobBatcherFewerJobsThanKmeansClusterTest(SlurmBatcherTestBase):
    def setUp(self):
        super().setUp(jobs=2, configs=1)

    def test_setup(self):
        self.batcher.setup()
        self.assertTrue(set(os.listdir(self.study.directory)).issuperset({"test_study_0.sbatch"}))

    def test_run(self):
        self.batcher.setup()
        self.batcher.run()

        self.assertEqual(len(self.study_files("config*")), 1)

        # 2 logs from directory, 1 of them are slurm logs
        self.assertEqual(len(self.study_files("config_*", "*.log")), 4)
        self.assertEqual(len(self.study_files("config*", "*.slurm.log")), 2)
