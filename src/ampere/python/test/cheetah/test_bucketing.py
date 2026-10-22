# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import unittest

from panthera.cheetah.bucketing import Bucketer
from panthera.cheetah.job import Job

TESTDIR = os.path.dirname(os.path.abspath(__file__))
FIXTURE_DIR = os.path.join(os.path.dirname(TESTDIR), "fixtures")


class TestBucketing(unittest.TestCase):
    def setUp(self):
        self.bucketer = Bucketer()


class TestBucketingMessages(TestBucketing):
    def test_ignore_numbers(self):
        messages = ["Error segfault 123", "Error segfault 234", "Error other", "Error other2"]
        self.bucketer.bucket(messages)
        self.assertEqual(3, len(self.bucketer.buckets))
        self.assertEqual("Error segfault ???", self.bucketer.buckets[0].key)
        self.assertEqual("Error other", self.bucketer.buckets[1].key)
        self.assertEqual("Error other2", self.bucketer.buckets[2].key)

    def test_hex_ignore_numbers(self):
        messages = [
            "Error segfault 0xdead",
            "Error segfault 0x1234",
            "Error: /ampere/panthera: ./bpu/src/ampere/core/bp_rsb.cc:80:void ampere::core::ReturnStack::commit_call(ampere::core::Fetch): sys.soc_0.core_0.bpu.tage.rsb: commit of non-tracked fetch id 3468, pc 0xffffb7167c00",
        ]

        self.bucketer.bucket(messages)
        self.assertEqual("Error segfault ???", self.bucketer.buckets[0].key)
        self.assertEqual(2, len(self.bucketer.buckets))

        self.assertEqual(
            "Error: /ampere/panthera: ./bpu/src/ampere/core/bp_rsb.cc:???:void ampere::core::ReturnStack::commit_call(ampere::core::Fetch): sys.soc_0.core_0.bpu.tage.rsb: commit of non-tracked fetch id ???, pc ???",
            self.bucketer.buckets[1].key,
        )

    def test_upper_case_hex_ignore_numbers(self):
        messages = ["Error segfault 0xDEAD12", "Error segfault 0x1234"]
        self.bucketer.bucket(messages)
        self.assertEqual("Error segfault ???", self.bucketer.buckets[0].key)
        self.assertEqual(1, len(self.bucketer.buckets))

    def test_ignore_numbers_hex_properties(self):
        messages = [
            "Error pc dead1234",
            "Error pc deadbeef",
            "Error virt_addr DEAD1234",
            "Error virt_addr DEADBEEF",
        ]
        self.bucketer.bucket(messages)
        self.assertEqual("Error pc ???", self.bucketer.buckets[0].key)
        self.assertEqual("Error virt_addr ???", self.bucketer.buckets[1].key)
        self.assertEqual(2, len(self.bucketer.buckets))


class TestBucketingWithLogs(TestBucketing):
    def setUp(self):
        super().setUp()
        self.jobs = [
            Job(trace, "echoone", os.path.join(FIXTURE_DIR, "studies", "echotest"))
            for trace in ("999.goodbye_7", "999.hello", "1000.python_07")
        ]
        for job in self.jobs:
            job.parse_log()
            job.error = "Error segfault"


class TestBucketingSorting(TestBucketingWithLogs):
    def assertBucketJobsAre(self, bucket, jobs):
        self.assertEqual([os.path.splitext(os.path.basename(l[0]))[0] for l in bucket.lines], jobs)

    def test_by_nothing(self):
        self.bucketer.bucket_jobs(self.jobs)
        self.assertEqual(1, len(self.bucketer.buckets))
        self.assertEqual("Error segfault", self.bucketer.buckets[0].key)
        self.assertBucketJobsAre(
            self.bucketer.buckets[0],
            [
                "999.goodbye_7",
                "999.hello",
                "1000.python_07",
            ],
        )

    def test_by_tick(self):
        self.bucketer.bucket_jobs(self.jobs, sort_by="tick")
        self.assertBucketJobsAre(
            self.bucketer.buckets[0],
            [
                "1000.python_07",
                "999.hello",
                "999.goodbye_7",
            ],
        )

    def test_by_run_metrics(self):
        for idx, job in enumerate(self.jobs):
            job.run_metrics["walltime"] = ((idx % 2) * 100) + idx + 1

        self.bucketer.bucket_jobs(self.jobs, sort_by="walltime")
        self.assertBucketJobsAre(
            self.bucketer.buckets[0],
            [
                "999.goodbye_7",
                "1000.python_07",
                "999.hello",
            ],
        )


class TestBucketingPrinting(TestBucketingWithLogs):
    def setUp(self):
        super().setUp()
        self.bucketer.bucket_jobs(self.jobs, sort_by="tick")

    def test_print_summary(self):
        self.bucketer.print_summary()

    def test_print_summary_with_jobs(self):
        self.bucketer.print_summary(4)

    def test_print_summary_table(self):
        self.bucketer.print_summary_table()

    def test_print_summary_table_with_jobs(self):
        self.bucketer.print_summary_table(4)
