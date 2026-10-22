# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import subprocess
import unittest
from os.path import dirname

from panthera import install_dir

test_dir = os.path.dirname(__file__)
slr_py = os.path.realpath(os.path.join(install_dir, "lib", "python", "panthera", "cheetah", "regression_generator.py"))
infile = os.path.join(test_dir, "test_regression.tlist")
outfile = os.path.join(test_dir, "test_regression.json")


class TestMaxWeightList(unittest.TestCase):
    def test_max_weight_list(self):
        pan = subprocess.run(["python3", slr_py, infile, outfile])
        self.assertTrue(os.access(outfile, os.R_OK), "{} does not exist".format(outfile))
        os.unlink(outfile)
