# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import unittest
from unittest.mock import Mock

from panthera.events import EventSpecification
from panthera.events.trace_events import EventSplitter, split_spec


class TestEventSpecification(unittest.TestCase):
    def test_event_only(self):
        es = EventSpecification("sys.soc_0.core_0.name")
        self.assertEqual(es.event, "sys.soc_0.core_0.name")
        self.assertEqual(es.factors, [])
        self.assertTrue(es("sys.soc_0.core_0.name"))

    def test_factored_event(self):
        es = EventSpecification("sys.soc_0.core_0.name/factor")
        self.assertEqual(es.event, "sys.soc_0.core_0.name")
        self.assertEqual(es.factors, ["factor"])
        self.assertEqual(es.long_factors, ["sys.soc_0.core_0.factor"])
        self.assertTrue(es("sys.soc_0.core_0.name"))

    def test_long_factored_event(self):
        es = EventSpecification("sys.soc_0.core_0.name/sys.soc_0.core_0.factor")
        self.assertEqual(es.event, "sys.soc_0.core_0.name")
        self.assertEqual(es.factors, ["sys.soc_0.core_0.factor"])
        self.assertEqual(es.long_factors, ["sys.soc_0.core_0.factor"])
        self.assertTrue(es("sys.soc_0.core_0.name"))


class TestEventSplitter(unittest.TestCase):
    def event(self, name, data=None, **kwargs):
        event = Mock(data=data or {}, **kwargs)
        event.name = name
        return event

    def test_split_basic(self):
        splitter = EventSplitter([split_spec("sys.soc_0.core_0.pmu/event=sys.soc_0.core_0.pmu.{event}")], include=True)
        self.assertIsNone(splitter(self.event("sys.soc_0.core_0.something"))[0])

        event = self.event("sys.soc_0.core_0.pmu", data={"sys.soc_0.core_0.event": 5})
        self.assertTrue(splitter(event)[0])
        self.assertEqual(event.name, "sys.soc_0.core_0.pmu.5")

    def test_split_lowercase(self):
        splitter = EventSplitter(
            [split_spec("sys.soc_0.core_0.pmu/event=sys.soc_0.core_0.pmu.{event:sl}")], include=True
        )

        event = self.event("sys.soc_0.core_0.pmu", data={"sys.soc_0.core_0.event": 5})
        self.assertTrue(splitter(event)[0])
        self.assertEqual(event.name, "sys.soc_0.core_0.pmu.5")

        event = self.event("sys.soc_0.core_0.pmu", data={"sys.soc_0.core_0.event": "SOMETHING"})
        self.assertTrue(splitter(event)[0])
        self.assertEqual(event.name, "sys.soc_0.core_0.pmu.something")
