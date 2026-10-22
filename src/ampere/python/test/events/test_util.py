# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import unittest

from panthera.events import EventSpecification


class TestUtil(unittest.TestCase):
    def test_event_specification_simple(self):
        inst_spec = EventSpecification("sys.soc_0.core_0.gpc.instruction_commit")

        self.assertTrue(inst_spec("sys.soc_0.core_0.gpc.instruction_commit"))
        self.assertFalse(inst_spec("sys.soc_0.core_0.gpc.uop_commit"))

    def test_event_specification_globs(self):
        send_spec = EventSpecification("sys.soc_0.core_#.send_req")

        self.assertTrue(send_spec("sys.soc_0.core_0.send_req"))
        self.assertTrue(send_spec("sys.soc_0.core_4.send_req"))
        self.assertTrue(send_spec("sys.soc_0.core_109.send_req"))
        self.assertFalse(send_spec("sys.soc_0.core_foo.send_req"))

        any_commit_spec = EventSpecification("sys.soc_0.core_0.gpc.?_commit")
        self.assertTrue(any_commit_spec("sys.soc_0.core_0.gpc.instruction_commit"))
        self.assertTrue(any_commit_spec("sys.soc_0.core_0.gpc.uop_commit"))
        self.assertTrue(any_commit_spec("sys.soc_0.core_0.gpc.uop_55_commit"))
        self.assertFalse(any_commit_spec("sys.soc_0.core_0.gpc.uop_complete"))

        any_gpc = EventSpecification("*gpc*")
        self.assertTrue(any_gpc("sys.soc_0.core_0.gpc.instruction_commit"))
        self.assertTrue(any_gpc("top.gpc.foo.bar"))
        self.assertFalse(any_gpc("sys.soc_0.core_0.idr.uop_decode"))
