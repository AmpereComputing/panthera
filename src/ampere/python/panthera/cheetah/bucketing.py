# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import itertools
import os
import re
from collections import namedtuple
from sys import maxsize
from typing import List

from .job import Job


class Bucket:
    def __init__(self, key, lines):
        self.key = key
        self.lines = lines

    @property
    def name(self):
        return self.key

    @property
    def size(self):
        return len(self.lines)


BucketLine = namedtuple("BucketLine", ["path", "sort_value", "info", "message"])


class Bucketer:
    pattern = re.compile(
        r"(?:\b[0-9a-f]*?\d[0-9a-f]+\b|\b0x[0-9a-f]+\b|\b-?\d+\b|\(?((?:pc|program_counter|addr) )(?:0x)?[0-9a-f]+\)?)",
        re.I,
    )

    @staticmethod
    def keyfunc(line):
        message = line[-1] if isinstance(line, tuple) else line
        return Bucketer.pattern.sub(r"\1???", message)

    def __init__(self):
        self.buckets = []

    def bucket(self, lines: List[str]):
        data = sorted(lines, key=Bucketer.keyfunc)
        buckets = []
        for k, g in itertools.groupby(data, Bucketer.keyfunc):
            bucket = Bucket(k, list(g))
            buckets.append(bucket)
        self.buckets = sorted(buckets, key=lambda b: len(b.lines), reverse=True)

    @classmethod
    def job_error_message(cls, job: Job):
        started = os.path.isfile(job.log_abspath)
        if job.error:
            return job.error if started else f"Incomplete ({job.error})"
        return f"Incomplete ({'still running' if started else 'not started'})"

    def bucket_jobs(self, jobs: List[Job], sort_by: str | None = None):
        def line_definition(job):
            path = job.log_abspath
            message = self.job_error_message(job)
            job_info = {}
            if sort_by == "tick" or sort_by is None:
                sort_value = job.tick
            else:
                sort_value = job.run_metrics.get(sort_by)
                if sort_value:
                    job_info[sort_by] = sort_value

            if job.tick:
                job_info["tick"] = job.tick

            return BucketLine(path, sort_value or maxsize, job_info, message)

        self.bucket([line_definition(job) for job in jobs])
        if sort_by is not None:
            for bucket in self.buckets:
                bucket.lines = list(sorted(bucket.lines, key=lambda l: l.sort_value))

    @classmethod
    def _job_signature(self, line: str | List[str] | BucketLine):
        if isinstance(line, str):
            return line
        if isinstance(line, list):
            return line[0]

        signature = line.path
        if line.info:
            signature += ": " + ", ".join(f"{k}={v}" for k, v in line.info.items())
        return signature

    def print_summary(self, jobs: int = 0):
        print("There are {} unique failure signatures\n".format(len(self.buckets)))
        total = sum(len(bucket.lines) for bucket in self.buckets)
        for idx, bucket in enumerate(self.buckets):
            bucket_size = len(bucket.lines)
            print(f"{idx + 1}. {bucket_size} jobs ({bucket_size / total:.1%} of failures): {bucket.key}")
            bucket_jobs = bucket.lines[:jobs]
            for line in bucket_jobs:
                print(f"  - {self._job_signature(line)}")
            if bucket_jobs and (idx + 1) != len(self.buckets):
                print()

    def print_summary_table(self, jobs: int = 0, line_length: int = 78):
        sig_length = line_length - 13

        def separator():
            print("-" * line_length)

        def row(count, signature):
            print(f"| {{:^6}} | {{:^{sig_length}.{sig_length}}} |".format(count, signature))

        print("There are {} unique failure signatures".format(len(self.buckets)))
        separator()
        row("Count", "Signature")
        separator()

        for idx, bucket in enumerate(self.buckets):
            row(len(bucket.lines), bucket.key)
            bucket_jobs = bucket.lines[:jobs]
            if bucket_jobs:
                separator()
            for line in bucket_jobs:
                row("-", self._job_signature(line))
            if bucket_jobs and (idx + 1) != len(self.buckets):
                separator()

        separator()
