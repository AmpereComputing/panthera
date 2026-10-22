# Copyright (c) 2020-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import re

from panthera.cheetah.job import Job
from panthera.cheetah.logfile import LogFile


class RegrLogFile(LogFile):
    vsim_failed_re = re.compile("^.*:I:fet: .* failed$")
    vsim_passed_re = re.compile("^.*:I:fet: .* passed$")

    def __init__(self, complete=True):
        super().__init__(complete=complete)

    def parse(self, log_path, *any_required_files):
        self.error = None
        self.complete = False
        if not os.path.isfile(log_path):
            self.error = "log missing"
        else:
            with open(log_path, "r") as logfile:
                for line in logfile:
                    failed = self.vsim_failed_re.match(line)
                    passed = self.vsim_passed_re.match(line)
                    if failed:
                        self.error = "vsim failed"
                        self.complete = True
                        break
                    elif passed:
                        self.complete = True
                        break


class RegrJob(Job):
    vsim_failed_re = re.compile("^.*:I:fet: .* failed$")
    vsim_passed_re = re.compile("^.*:I:fet: .* passed$")

    @classmethod
    def parser(cls):
        return RegrLogFile

    def run(self):
        raise RuntimeError("run() is not implemented for RegrJob, which links to already-completed RTL regression runs")

    def parse_from_log(self, logfile):
        super().parse_from_log(logfile)
        if "event_stream_error" in self.meta:
            self.error = self.meta["event_stream_error"]
