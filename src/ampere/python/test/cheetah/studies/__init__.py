# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import shlex
import unittest
from pathlib import Path

ROOTDIR = str(Path(__file__).absolute().parents[6])
WORKDIR = str(Path(os.getcwd()).absolute().parents[2])


class BaseSimTest(unittest.TestCase):
    def setUp(self):
        self.maxDiff = None
        self.trace = {
            "name": "something",
            "file": "something.is.xz",
        }
        self.settings = {
            "panthera_root": "./install",
        }

    def normalize(self, command_str):
        if isinstance(command_str, list):
            command_str = " ".join(command_str)
        command_str = command_str.replace(WORKDIR, "./build").replace(ROOTDIR, ".")
        return shlex.join(shlex.split(command_str))

    def render(self, cmd):
        return self.normalize(cmd.render())
