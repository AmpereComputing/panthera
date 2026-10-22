# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import unittest

import panthera.streams.event_stream_pb2 as es_proto
from panthera.streams.event_stream import EventStreamReader

testdir = os.path.dirname(__file__)


class TestEventStreamReader(unittest.TestCase):
    def setUp(self):
        self.event = es_proto.Event()
        self.reader = EventStreamReader(os.path.join(testdir, "four_events_with_data.es"))

    def test_read_one_event(self):
        self.reader.read_event(self.event)
        self.assertEqual(self.event.definition_id, 1)
        self.assertEqual(self.event.time, 200)
        self.assertEqual(len(self.event.values), 2)

    def test_read_events(self):
        events = list(self.reader.read_events({"foo"}))
        self.assertEqual(len(events), 2)
        self.assertEqual(events[0].name, "foo")
        self.assertEqual(
            events[0].data,
            {
                "bar": 1234,
                "baz": 5678,
            },
        )
        self.assertEqual(events[1].time, 400)
        self.assertEqual(events[1].data, {})
