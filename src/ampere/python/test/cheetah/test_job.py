# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import unittest

from panthera.cheetah.cmd import Cmd, CmdSeq
from panthera.cheetah.job import Job


class TestCommandRemovalInJob(unittest.TestCase):
    def cmdseq(self, only_if):
        return CmdSeq(
            [
                Cmd(
                    cmd="echo {number}",
                    number=i,
                    name=str(i),
                    directory="something",
                    only_if=only_if,
                )
                for i in range(3)
            ]
        )

    def test_already_empty(self):
        with self.assertRaises(ValueError):
            Job.from_cmds(CmdSeq([]), "something")

    def test_one_removed(self):
        cmds = self.cmdseq(lambda c: c["number"] != 1)
        job = Job.from_cmds(cmds, "something")
        self.assertEqual(len(job.cmds), 2)

    def test_all_removed(self):
        cmds = self.cmdseq(lambda c: False)
        with self.assertRaises(ValueError):
            Job.from_cmds(cmds, "something")
