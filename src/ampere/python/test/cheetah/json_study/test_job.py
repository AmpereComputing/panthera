# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import unittest
from tempfile import TemporaryDirectory

from panthera.cheetah.job import Job

FIXTURE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "../fixtures"))


class TestJob(unittest.TestCase):
    def test_init(self):
        job = Job("simple", "simple_dir", ".")
        self.assertEqual("simple", job.name)
        self.assertEqual("simple_dir", job.directory)

    def test_init_with_cmds(self):
        job = Job("simple", "simple_dir", ".", cmds=[["echo"]])
        self.assertEqual("simple", job.name)
        self.assertEqual("simple_dir", job.directory)
        self.assertEqual([["echo"]], job.cmds)

    def test_make_filename(self):
        job = Job("simple", "simple_dir", ".")
        self.assertEqual("simple_dir/simple.txt", job.make_filename(".txt"))

    def test_to_json(self):
        meta = {"workload": "Hello"}
        cmds = [["echo", "Hello World!"]]
        job = Job("simple", "simple_dir", ".", cmds=cmds, meta=meta)
        expect = {"name": "simple", "directory": "simple_dir", "cmds": cmds, "meta": meta}
        self.assertEqual(expect, job.to_json())


class TestJobRun(unittest.TestCase):
    def setUp(self):
        self.working_dir = TemporaryDirectory()
        self.cwd = os.getcwd()
        self.job = Job("hello", ".", self.working_dir.name, [["echo", "Hello World!"]])
        os.chdir(self.working_dir.name)

    def tearDown(self):
        os.chdir(self.cwd)
        self.working_dir.cleanup()

    def verify_job_log(self, lines):
        self.assertEqual(4, len(lines))
        # 'echo' and 'Hello World!' are checked for separately, because our
        # commandlines are passed through shlex.join() which can add quotes
        self.assertIn("echo", lines[0])
        self.assertIn("Hello World!", lines[0])

        self.assertEqual("Hello World!\n", lines[1])
        self.assertEqual("Command successful exit.\n", lines[2])
        self.assertEqual("All commands exited successfully.\n", lines[3])

    def test_run(self):
        self.job.run()
        with open(self.job.log) as f:
            self.verify_job_log(f.readlines())

    def test_run_chdir(self):
        os.chdir(self.cwd)
        self.job.run(chdir=True)
        with open(self.job.log_abspath) as f:
            self.verify_job_log(f.readlines())

    def test_run_shell(self):
        self.job = Job("hello", ".", ".", [["echo", "Hello World!", ">", "output.txt"]])
        self.job.run(shell=True)
        with open("output.txt") as f:
            lines = f.readlines()
            self.assertIn("Hello World!", lines[0])

        with open(self.job.log) as f:
            self.assertEqual(3, len(f.readlines()))

    def test_run_one_in_shell(self):
        self.job = Job(
            "hello",
            ".",
            ".",
            [["touch", "output1.txt"], ["echo", "Hello World!", ">", "output2.txt"]],
            {},
            [{}, {"run_in_shell": True}],
        )
        self.job.run(shell=True)

        with open("output1.txt") as f:
            lines = f.readlines()
            self.assertEqual(0, len(lines))

        with open("output2.txt") as f:
            lines = f.readlines()
            self.assertIn("Hello World!", lines[0])

        with open(self.job.log) as f:
            self.assertEqual(5, len(f.readlines()))
