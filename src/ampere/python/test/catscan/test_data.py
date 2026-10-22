# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import copy
import unittest
from os.path import abspath, dirname, join

import panthera.events.trace_events
from panthera.catscan.data import *
from panthera.events import matching_spec, rename_spec
from panthera.events.mapping import Mapper

TESTDIR = dirname(dirname(abspath(__file__)))


class CatscanDataTest(unittest.TestCase):
    def __init__(self, *args, **kwargs):
        self._esd = self.get_event_data()
        super().__init__(*args, **kwargs)

    def setUp(self):
        self.esd = copy.deepcopy(self._esd)

    def get_event_data(self):
        events = [trace_events.trace_spec("core.gpc.flush")]

        # Note: The splits are intentionally placed in the middle of the
        # definitions of the other events so that we can test that the order of
        # initialization of the EventSpecification objects for each determines
        # the eventual order of the event rows
        splits = [trace_events.split_spec("core.bpu.tage.btb.read/hit=btb_read_hit_{hit}")]
        renames = [rename_spec("btb_read_hit_1=btb_hit")]
        excludes = [trace_events.trace_spec("btb_read_hit_[0]")]

        event_names = [
            "core.gpc.flush",
            "core.bpu.br_train",
            "core.icf.cache.access",
            "core.icf.iq_write",
            "core.idr.align",
            "core.idr.uop_dispatch",
            "core.ixu.uop_issue",
            "core.dmmu.tlb_lookup",
            "core.gpc.uop_commit",
            "core.gpc.instruction_commit",
        ]
        events += [trace_events.trace_spec(e) for e in event_names]
        instargs = ["core.icf.inst_bytes", "core.gpc.commit_instruction"]
        post_to_tx = ["core.icf.iq_write:core.icf.inst_bytes"]
        pull_from_tx = ["core.gpc.instruction_commit:core.icf.inst_bytes", "core.dmmu.tlb_lookup:core.icf.inst_bytes"]
        event_filters = trace_events.EventFilters(
            [
                trace_events.EventSplitter(splits),
                trace_events.EventRenamer(renames),
                trace_events.EventIncluder(events),
                trace_events.EventExcluder(excludes),
            ],
        )

        self._pct_loaded = 0

        def pct_loaded_callback():
            def handle(pct):
                self._pct_loaded = pct

            return handle

        filename = join(TESTDIR, "test_events.es")
        mapper = Mapper([], [], instargs)

        return get_event_data(
            filename,
            "resource",
            mapper=mapper,
            event_filters=event_filters,
            events=events,
            post_to_tx=post_to_tx,
            pull_from_tx=pull_from_tx,
            occupancy=["FEQ:core.bpu.feq.acquire,core.bpu.feq.release"],
            pct_loaded_callback=pct_loaded_callback(),
        )


class TestData(CatscanDataTest):
    def test_pct_callback(self):
        # Check we are reporting file completion
        self.assertAlmostEqual(self._pct_loaded, 100, delta=5)

    def test_first_last_time(self):
        # Check the overall first/last times were correctly discovered
        self.assertEqual(self.esd.first_time, 250083)  # first of these events is a GPC flush
        self.assertEqual(self.esd.last_time, 525474)  # final instruction_commit

    def test_transactions(self):
        # Check the transactions are built up correctly
        # txid=349 is a DMMU request from the LSU
        txid = 349
        self.assertIn(txid, self.esd.transactions)

        txn = self.esd.transactions[txid]
        self.assertEqual(txn.start_time, 256743)
        self.assertEqual(txn.end_time, 362970)

        # four children: empty txid (359), two L2C requests (444, 512), the MMU response to the LSU (643)
        self.assertEqual(txn.children, set((359, 444, 512, 643)))
        # one parent, the uop (272)
        self.assertEqual(txn.parents, set((272,)))

    def test_event_counts(self):
        # Check per-event counts are accurate
        self.assertEqual(self.esd.event_rows["core.gpc.flush"].event_count, 6)
        self.assertEqual(len(self.esd.event_rows["core.gpc.flush"]), 6)
        self.assertEqual(self.esd.event_rows["core.gpc.instruction_commit"].event_count, 100)
        self.assertEqual(len(self.esd.event_rows["core.gpc.instruction_commit"]), 100)
        self.assertEqual(self.esd.event_rows["core.dmmu.tlb_lookup"].event_count, 2)
        self.assertEqual(len(self.esd.event_rows["core.dmmu.tlb_lookup"]), 2)
        self.assertEqual(len(self.esd.event_rows["btb_hit"]), 5)

        # Check overall event count is accurate
        self.assertEqual(self.esd.event_count, 6 + 9 + 115 + 599 + 460 + 461 + 344 + 2 + 119 + 100 + 5 + 241)

    def test_max_events_per_time(self):
        # Check max_events_per_time() on the event rows
        self.assertEqual(self.esd.event_rows["core.dmmu.tlb_lookup"].max_events_per_time(), 1)
        self.assertEqual(self.esd.event_rows["core.gpc.uop_commit"].max_events_per_time(), 4)
        self.assertEqual(self.esd.event_rows["core.gpc.instruction_commit"].max_events_per_time(), 5)

    def test_event_range_simple(self):
        inst_commit = self.esd.event_rows["core.gpc.instruction_commit"]

        # Check finding single event, excluding end
        found = list(inst_commit[254412:428238])
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0].time, 254412)

    def test_event_range_skipping(self):
        inst_commit = self.esd.event_rows["core.gpc.instruction_commit"]

        # Check stepping (skipping) some events
        found = list(inst_commit[254412:431235:2])
        self.assertEqual(len(found), 2)
        self.assertEqual(found[0].time, 428238)
        self.assertEqual(found[0].data["core.gpc.inum"], 2)
        self.assertEqual(found[1].time, 428238)
        self.assertEqual(found[1].data["core.gpc.inum"], 4)

    def test_event_range_skipping_reverse(self):
        inst_commit = self.esd.event_rows["core.gpc.instruction_commit"]

        # Check stepping (skipping) some events in reverse
        found = list(inst_commit[428238:254411:-2])
        self.assertEqual(len(found), 2)
        self.assertEqual(found[0].time, 428238)
        self.assertEqual(found[0].data["core.gpc.inum"], 3)
        self.assertEqual(found[1].time, 254412)
        self.assertEqual(found[1].data["core.gpc.inum"], 1)

    def test_single_event_time(self):
        inst_commit = self.esd.event_rows["core.gpc.instruction_commit"]

        # Check indexing by a single event start time
        found = list(inst_commit[520479])
        self.assertEqual(len(found), 3)
        self.assertEqual(found[0].time, 520479)
        self.assertEqual(found[0].data["core.gpc.inum"], 45)
        self.assertEqual(found[1].time, 520479)
        self.assertEqual(found[1].data["core.gpc.inum"], 46)
        self.assertEqual(found[2].time, 520479)
        self.assertEqual(found[2].data["core.gpc.inum"], 47)

    def test_younger_older(self):
        uop_issue = self.esd.event_rows["core.ixu.uop_issue"]

        uop_a = list(uop_issue[523809])[0]
        uop_b = list(uop_issue[523809])[1]
        uop_c = list(uop_issue[523809])[2]
        uop_d = list(uop_issue[523809])[3]
        uop_e = list(uop_issue[524142])[0]
        uop_f = list(uop_issue[524142])[1]
        uop_g = list(uop_issue[524142])[2]

        # Ensure we have the expected uop to start
        self.assertEqual(uop_a.data["txid"], 1533)
        self.assertEqual(uop_b.data["txid"], 1565)
        self.assertEqual(uop_c.data["txid"], 1527)
        self.assertEqual(uop_d.data["txid"], 1558)
        self.assertEqual(uop_e.data["txid"], 1571)
        self.assertEqual(uop_f.data["txid"], 1570)
        self.assertEqual(uop_g.data["txid"], 1567)

        self.assertEqual(uop_issue.oldest_younger(uop_a), uop_b)
        self.assertEqual(uop_issue.oldest_younger(uop_b), uop_c)
        self.assertEqual(uop_issue.oldest_younger(uop_c), uop_d)
        self.assertEqual(uop_issue.oldest_younger(uop_d), uop_e)
        self.assertEqual(uop_issue.oldest_younger(uop_f), uop_g)

        self.assertEqual(uop_issue.youngest_older(uop_b), uop_a)
        self.assertEqual(uop_issue.youngest_older(uop_c), uop_b)
        self.assertEqual(uop_issue.youngest_older(uop_d), uop_c)
        self.assertEqual(uop_issue.youngest_older(uop_e), uop_d)
        self.assertEqual(uop_issue.youngest_older(uop_g), uop_f)

    def test_closest_to(self):
        inst_commit = self.esd.event_rows["core.gpc.instruction_commit"]

        def assertClosestTo(time, inst):
            self.assertEqual(inst_commit.closest_to(time).data["core.gpc.inum"], inst)

        # All of these should resolve to the first instruction
        assertClosestTo(-10, 1)
        assertClosestTo(254411, 1)
        assertClosestTo(254412, 1)
        assertClosestTo(300000, 1)

        # Then we should switch to the next cycle/instruction once we pass the
        # midway point between them
        assertClosestTo(400000, 2)
        assertClosestTo(428237, 2)
        # If we are precisely on the same time as several events, we pick the
        # "middle" one
        assertClosestTo(428238, 3)
        # If the time is after the closest, we pick the last event in the cycle
        assertClosestTo(428538, 4)

        # Check that this one "rounds down"
        assertClosestTo(525300, 96)

        # Check that last cycle's worth of instructions working right
        assertClosestTo(525353, 97)
        assertClosestTo(525474, 98)
        assertClosestTo(999999, 100)

    def test_ancestors(self):
        uop_issue = self.esd.event_rows["core.ixu.uop_issue"]
        # fetch: txid=1501
        # instruction: txid=1539
        # uop: txid=1586

        txid = 1586
        uop = list(uop_issue[524475])[0]
        self.assertEqual(uop.data["txid"], txid)

        sort_txns = lambda txns: sorted(txns, key=lambda txn: txn.txid)

        # Three ways to get ancestors
        event_ancestors = sort_txns(self.esd.ancestors(uop))
        txid_ancestors = sort_txns(self.esd.ancestors(txid))
        transaction_ancestors = sort_txns(self.esd.ancestors(self.esd.transactions[txid]))
        expected_ancestors = sort_txns([self.esd.transactions[t] for t in [1501, 1539]])

        self.assertEqual(event_ancestors, expected_ancestors)
        self.assertEqual(txid_ancestors, expected_ancestors)
        self.assertEqual(transaction_ancestors, expected_ancestors)

    def test_descendants(self):
        icf_cache = self.esd.event_rows["core.icf.cache.access"]
        # fetch: txid=1477
        # instructions: txid=1529, 1530, 1531
        # uops: txid=1577, 1578 (instruction 1531 never creates a uop)

        txid = 1477
        fetch = list(icf_cache[519813])[0]
        self.assertEqual(fetch.data["txid"], txid)

        sort_txns = lambda txns: sorted(txns, key=lambda txn: txn.txid)
        event_descendants = sort_txns(self.esd.descendants(fetch))
        txid_descendants = sort_txns(self.esd.descendants(txid))
        transaction_descendants = sort_txns(self.esd.descendants(self.esd.transactions[txid]))
        expected_descendants = sort_txns([self.esd.transactions[t] for t in [1529, 1530, 1531, 1577, 1578]])

        self.assertEqual(event_descendants, expected_descendants)
        self.assertEqual(txid_descendants, expected_descendants)
        self.assertEqual(transaction_descendants, expected_descendants)

    def test_event_row_keys(self):
        expected_row_keys = (
            "core.gpc.flush",
            # Note: btb_hit, a "split", should be placed here because it was
            # initialized in this order relative to the other events
            "btb_hit",
            "core.bpu.br_train",
            "core.icf.cache.access",
            "core.icf.iq_write",
            "core.idr.align",
            "core.idr.uop_dispatch",
            "core.ixu.uop_issue",
            "core.dmmu.tlb_lookup",
            "core.gpc.uop_commit",
            "core.gpc.instruction_commit",
            "FEQ",
        )
        self.assertEqual(self.esd.event_row_keys, expected_row_keys)

    def test_post_to_pull_from_tx(self):
        # Note: core.icf.inst_bytes is being pulled from core.icf.iq_write and
        # pushed to core.gpc.instruction_commit and core.dmmu.tlb_lookup events
        # in the common setup code above.
        inst_commit = self.esd.event_rows["core.gpc.instruction_commit"]
        tlb_lookup = self.esd.event_rows["core.dmmu.tlb_lookup"]

        # Spot-check that a few fields have been properly posted/pulled from
        # the transaction
        inst_a = list(inst_commit[432234])[0]
        self.assertEqual(inst_a.data["core.icf.inst_bytes"], hex(2601982342))

        inst_b = list(inst_commit[525474])[1]
        self.assertEqual(inst_b.data["core.icf.inst_bytes"], hex(3107979308))

        tlb_lookup_a = list(tlb_lookup[256743])[0]
        self.assertEqual(tlb_lookup_a.data["core.icf.inst_bytes"], hex(3093326314))

        tlb_lookup_b = list(tlb_lookup[257076])[0]
        self.assertEqual(tlb_lookup_b.data["core.icf.inst_bytes"], hex(4163992259))

    def test_occupancy(self):
        feq = self.esd.event_rows["FEQ"]
        self.assertEqual(feq.event_count, 241)

        prev = 0
        non_zero = 0
        for events in feq.events.values():
            for event in events:
                count = int(event.abbrev)
                self.assertTrue(count == 0 or (count > 0 and abs(count - prev) == 1))
                if count > 1:
                    non_zero += 1
                prev = count

        self.assertGreater(non_zero, 0)
