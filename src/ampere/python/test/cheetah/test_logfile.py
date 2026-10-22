# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import unittest

from panthera.cheetah.job import LogFile

FIXTURE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "../fixtures"))


class TestLogFile(unittest.TestCase):
    def parse_log(self, name):
        return LogFile.from_file(os.path.join(FIXTURE_DIR, "logs", f"{name}.log"))

    def test_success(self):
        log = self.parse_log("successful")
        self.assertTrue(log.complete)
        self.assertFalse(log.error)

    def test_success_no_simulation(self):
        log = self.parse_log("successful_no_simulation")
        self.assertTrue(log.complete)
        self.assertFalse(log.error)

    def test_tick(self):
        log = self.parse_log("successful")
        self.assertEqual(log.tick, 1436487741)

    def test_run_metrics(self):
        log = self.parse_log("successful")
        self.assertEqual(log.run_metrics["cputime"], 28)
        self.assertEqual(log.run_metrics["walltime"], 36)

    def test_error(self):
        log = self.parse_log("error")
        self.assertFalse(log.complete)
        self.assertRegex(log.error, r".*void ampere::xxx::XXX::something\(\): some error.*")

    def test_error_in_warmup(self):
        log = self.parse_log("error_in_warmup")
        self.assertFalse(log.complete)
        self.assertRegex(log.error, r".*void ampere::xxx::XXX::something\(\): some error.*")
        self.assertEqual(log.run_metrics["cputime"], 11)
        self.assertEqual(log.run_metrics["walltime"], 13)

    def test_fatal(self):
        log = self.parse_log("fatal")
        self.assertFalse(log.complete)
        self.assertRegex(log.error, r".*void ampere::xxx::XXX::something\(\): some fatal error.*")

    def test_incomplete(self):
        log = self.parse_log("incomplete")
        self.assertFalse(log.complete)
        self.assertFalse(log.error)

    def test_post_error(self):
        log = self.parse_log("post_error")
        self.assertFalse(log.complete)
        self.assertRegex(log.error, r".*bash do_something.sh*")

    def test_evp_early_exit(self):
        log = self.parse_log("evp_early_exit")
        self.assertTrue(log.complete)
        self.assertFalse(log.error)

    def test_require_file(self):
        log = LogFile.from_file(
            os.path.join(FIXTURE_DIR, "logs", "successful.log"),
            os.path.join(FIXTURE_DIR, "studies", "echotest", "echoone", "999.goodbye_7.ts.csv.gz"),
        )
        self.assertTrue(log.complete)
        self.assertFalse(log.error)

    def test_require_file_missing(self):
        log = LogFile.from_file(os.path.join(FIXTURE_DIR, "logs", "successful.log"), "missing.summary.csv")
        self.assertFalse(log.complete)
        self.assertFalse(log.error)
