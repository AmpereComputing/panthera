# Copyright (c) 2024-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import unittest
from os.path import dirname, join
from types import SimpleNamespace

from panthera.events.mapping import Mapper, use_adis
from panthera.streams.event_stream import EventStreamReader

test_dir = dirname(dirname(__file__))


class TestMapping(unittest.TestCase):
    def get_event_stream(self) -> EventStreamReader:
        filename = join(test_dir, "test_events.es")
        return EventStreamReader(filename, all_events=True)

    def test_mapping_memory_states(self):
        cases = [
            ("sys.soc_0.memory.mc_cmd", "bt_state", 3),
            ("sys.soc_0.memory.arb.mode", "mc_arb_mode", 2),
            ("sys.soc_0.memory.chi_mode", "chi_mode", 1),
        ]

        mapper = Mapper([], [], [])
        for event_name, field_name, value in cases:
            with self.subTest(event_name=event_name):
                event = SimpleNamespace(name=event_name, data={f"sys.soc_0.memory.{field_name}": value})
                self.assertEqual(mapper.event_name_to_abbrev(event), str(value))

    @unittest.skipUnless(use_adis, "Expected ADIS for disasm")
    def test_mapping_iq_write(self):
        es = self.get_event_stream()
        mapper = Mapper([], [], [])
        iq_event_reader = es.read_events(["core.icf.iq_write"])
        iq_events = [next(iq_event_reader) for _ in range(20)]
        iq_event_abbreviations = [mapper.event_name_to_abbrev(event) for event in iq_events]

        # Here is a current listing of the disassembly for the first 20 IQ
        # writes. We'll spot check only a few and make sure they contain some
        # of the right pieces:

        # 0: 'sbfm x6, x5, #0, #31',
        self.assertIn("sbfm", iq_event_abbreviations[0])
        # 1: 'ldr w11, [x11, (x28){, lsl#0]',
        # 2: 'sbfm x14, x4, #0, #31',
        # 3: 'madd x6, x6, x21, x8',
        # 4: 'ldr w12, [x1, #0]',
        # 5: 'sbfm x18, x3, #0, #31',
        # 6: 'sbfm x17, x10, #0, #31',
        # 7: 'madd x14, x14, x21, x8',
        # 8: 'b.le 207 ',
        self.assertIn("b.le", iq_event_abbreviations[8])
        # 9: 'sbfm x11, x11, #0, #31',
        # 10: 'sbfm x12, x12, #0, #31',
        # 11: 'madd x6, x11, x24, x6',
        # 12: 'madd x18, x18, x24, x14',
        # 13: 'madd x6, x12, x23, x6',
        # 14: 'madd x17, x17, x23, x18',
        self.assertIn("madd", iq_event_abbreviations[14])
        # 15: 'ldr x3, [x22, (x6){, lsl#3]',
        # 16: 'str x3, [x22, (x17){, lsl#3]',
        self.assertIn("str", iq_event_abbreviations[16])
        # 17: 'add w9, w9, #1, lsl #0',
        # 18: 'add x2, x2, x13, lsl#0',
        self.assertIn("add", iq_event_abbreviations[18])
        # 19: 'subs wzr, w9, w25, lsl#0'
        self.assertIn("subs", iq_event_abbreviations[19])
