# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from panthera.catscan.search import *
from panthera.catscan.util import hex_args_to_re
from test_data import CatscanDataTest


class TestSearch(CatscanDataTest):
    def test_get_search_fields(self):
        inum_fields = FilteringSearcher.get_search_fields(self.esd, ["*.inum"])
        self.assertEqual(inum_fields, set(("core.gpc.inum",)))

        hit_fields = FilteringSearcher.get_search_fields(self.esd, ["*.hit"])
        self.assertEqual(hit_fields, set(("core.icf.cache.hit", "core.bpu.tage.btb.hit")))

    def test_basic_text_search(self):
        bi_modal_searcher = TextSearcher("bi_modal", None, None, False, hex_args_to_re(["program_counter"]))

        # Ensure we're told to search all fields and all rows
        self.assertFalse(bi_modal_searcher.filtering_fields())
        self.assertTrue(bi_modal_searcher.search_row("foobar", []))
        self.assertTrue(bi_modal_searcher.search_row("core.gpc.instruction_commit", []))

        br_train = list(self.esd.event_rows["core.bpu.br_train"][431901])[0]
        icache_access = list(self.esd.event_rows["core.icf.cache.access"][519813])[0]

        self.assertTrue(bi_modal_searcher.match(br_train))
        self.assertFalse(bi_modal_searcher.match(icache_access))

    def test_basic_text_search_hex_number(self):
        full_address_searcher = TextSearcher("0x648910", None, None, False, hex_args_to_re(["program_counter"]))
        partial_address_searcher = TextSearcher("0x6489", None, None, False, hex_args_to_re(["program_counter"]))

        flush = list(self.esd.event_rows["core.gpc.flush"][250083])[0]
        iq_write = list(self.esd.event_rows["core.icf.iq_write"][519813])[0]

        # Only flush matches, because its program_counter is 0x648910 exactly
        self.assertTrue(full_address_searcher.match(flush))
        self.assertFalse(full_address_searcher.match(iq_write))

        # Doing a partial string search allows the iq_write to match, because
        # its PC is 0x648920
        self.assertTrue(partial_address_searcher.match(flush))
        self.assertTrue(partial_address_searcher.match(iq_write))

    def test_integer_search(self):
        address_searcher = IntSearcher(0x648910, None, None, None)
        masked_address_searcher = IntSearcher(0x648910, None, None, 0xFFFFC0)

        flush = list(self.esd.event_rows["core.gpc.flush"][250083])[0]
        iq_write = list(self.esd.event_rows["core.icf.iq_write"][519813])[0]

        # Only flush matches, because its program_counter is 0x648910 exactly
        self.assertTrue(address_searcher.match(flush))
        self.assertFalse(address_searcher.match(iq_write))

        # Doing a partial string search allows the iq_write to match, because
        # its PC is 0x648920
        self.assertTrue(masked_address_searcher.match(flush))
        self.assertTrue(masked_address_searcher.match(iq_write))

    def test_text_search_tracking(self):
        bi_modal_searcher = TextSearcher("bi_modal", None, None, False, hex_args_to_re(["program_counter"]))
        starting_br_train = list(self.esd.event_rows["core.bpu.br_train"][517149])[0]

        self.assertEqual(starting_br_train.data["txid"], 284)  # Make sure we have the right one to start

        data_search = EventStreamDataSearch(self.esd.events(), bi_modal_searcher, starting_br_train)

        # Should be 9 total matches
        self.assertEqual(data_search.total_matches, 9)
        # The 'next' match (which is the same as the starting point) is the
        # third overall match (0-indexed)
        self.assertEqual(data_search.cursor_idx, 2)
        self.assertEqual(data_search.search_cursor.data["txid"], 284)

        # Advance the search cursor by one
        next_match = data_search.next()
        self.assertEqual(data_search.cursor_idx, 3)
        self.assertEqual(next_match.data["txid"], 716)

        # Force a wrap-around by advancing the search cursor 6 times
        for i in range(6):
            next_match = data_search.next()

        # We should now be at the beginning
        self.assertEqual(data_search.cursor_idx, 0)
        self.assertEqual(next_match.data["txid"], 228)

        prev_match = data_search.prev()

        # We should now be at the beginning
        self.assertEqual(data_search.cursor_idx, 8)
        self.assertEqual(prev_match.data["txid"], 1015)

    def test_filtered_integer_search_tracking(self):
        # search for instruction commits which share half-cacheline PCs with
        # 0x648914. Furthermore, this is filtered by both row and field name to
        # test avoiding finding searches on other events that may share these
        # PCs (in particular *.uop_commit) would collide
        searcher = IntSearcher(
            0x648914,
            search_rows=["core.gpc.instruction_commit"],
            search_fields=["core.gpc.commit_pc"],
            search_mask=0xFFFFFFFFFFFFFFE0,
        )
        starting_commit = list(self.esd.event_rows["core.gpc.instruction_commit"][516816])[0]
        self.assertEqual(starting_commit.data["txid"], 276)  # Make sure we have the right one to start

        data_search = EventStreamDataSearch(self.esd.events(), searcher, starting_commit)

        # Should be 30 total matches
        self.assertEqual(data_search.total_matches, 15)
        # The 'next' match (which is the same as the starting point) is the
        # fourth overall match (0-indexed)
        self.assertEqual(data_search.search_cursor.data["txid"], 682)
        self.assertEqual(data_search.cursor_idx, 3)

        # Look through 'previous' matches
        data_search.prev()
        data_search.prev()
        prev_match = data_search.prev()

        self.assertEqual(data_search.search_cursor.data["txid"], 196)
        self.assertEqual(prev_match.data["txid"], 196)
        self.assertEqual(data_search.cursor_idx, 0)

        # Wrap back around
        prev_match = data_search.prev()

        self.assertEqual(data_search.search_cursor.data["txid"], 1113)
        self.assertEqual(prev_match.data["txid"], 1113)
        self.assertEqual(data_search.cursor_idx, 14)


class TestPerPeriodSearch(CatscanDataTest):
    def test_any(self):
        all_searcher = TextSearcher("*", None, None, False, hex_args_to_re(["program_counter"]))
        searcher = PerPeriodSearcher(4, all_searcher)

        self.assertTrue(searcher.search_row("core.gpc.uop_commit", []))

        for time in [428238, 431235]:
            commit = list(self.esd.event_rows["core.gpc.uop_commit"][time])
            self.assertFalse(searcher.match(commit[0]))
            self.assertFalse(searcher.match(commit[1]))
            self.assertFalse(searcher.match(commit[2]))
            self.assertTrue(searcher.match(commit[3]))
