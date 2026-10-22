# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import json
import os
import subprocess
import tempfile
import unittest
from os.path import dirname, join

from panthera import install_dir

panthera_py = os.path.realpath(join(install_dir, "bin", "panthera"))

test_dir = dirname(dirname(__file__))


class TestTracing(unittest.TestCase):
    def trace_events(self, *args, **kwargs):
        event_stream = kwargs.pop("event_stream", "test_events.es")
        with tempfile.TemporaryDirectory() as tmpdirname:
            cmd = [
                panthera_py,
                "events.tracing",
                join(test_dir, event_stream),
                "-o",
                join(tmpdirname, "test_events.json"),
            ] + list(args)
            subprocess.run(cmd, check=True)
            with open(join(tmpdirname, "test_events.json")) as test_events:
                return json.loads(test_events.read())

    def _event_matches(self, event, args, partial=False):
        if isinstance(args, dict):
            for k, v in args.items():
                if not k in event:
                    return False
                matches = self._event_matches(event[k], v, partial=partial) if partial else event[k] == v
                if not matches:
                    return False

        elif isinstance(args, set):
            for k in args:
                if not k in event:
                    return False

        elif callable(args):
            return args(event)

        elif event != args:
            return False

        return True

    def find(self, trace, **kwargs):
        return [event for event in trace if self._event_matches(event, kwargs)]

    def find_partial(self, trace, **kwargs):
        return [event for event in trace if self._event_matches(event, kwargs, partial=True)]

    def find_events_by_name(self, trace, name):
        threads = self.find(trace, name="thread_name", args={"name": name})
        if not threads:
            raise KeyError(name)

        events = self.find(trace, tid=threads[0]["tid"])
        return events

    def event_exists(self, trace, name):
        try:
            self.find_events_by_name(trace, name)
            return True
        except KeyError:
            return False

    def find_event_by_name(self, trace, name, txid=None):
        events = self.find_events_by_name(trace, name)
        if txid is None:
            return events[0]
        else:
            for event in events:
                if event["args"]["txid"] == txid:
                    return event
        return None

    def test_trace_contains_start_event(self):
        trace = self.trace_events()
        self.assertEqual(trace[-1]["cat"], "start")
        self.assertEqual(trace[-1]["ts"], 0)

    def test_select_event(self):
        trace = self.trace_events("--event", "core.idr.frontend_stall")
        fe_stall_thread = self.find(trace, name="thread_name", args={"name": "core.idr.frontend_stall"})[0]
        self.assertTrue(fe_stall_thread)
        fe_stalls = self.find(trace, tid=fe_stall_thread["tid"])
        self.assertGreater(len(fe_stalls), 10)

    def test_tx_data(self):
        trace = self.trace_events(
            "--event", "core.gpc.uop_commit",
            "--event", "core.icf.iq_write",
            "--post_to_tx", "core.icf.iq_write:program_counter",
            "--pull_from_tx", "core.gpc.uop_commit:program_counter",
        )  # fmt: skip
        uop_commit_thread = self.find(trace, name="thread_name", args={"name": "core.gpc.uop_commit"})[0]
        self.assertTrue(uop_commit_thread)
        uop_commits = self.find(trace, tid=uop_commit_thread["tid"])
        for uop in uop_commits:
            if uop["name"].startswith("thread_"):
                continue
            self.assertTrue("program_counter" in uop["args"])

    def test_parents(self):
        trace = self.trace_events(
            "--event", "core.bpu.feq.acquire",  # no parent
            "--event", "core.icf.iq_write",  # one parent
            "--event", "core.idr.uop_decode",  # two parents
        )  # fmt: skip
        feq_acquire = self.find_event_by_name(trace, "core.bpu.feq.acquire")
        iq_write = self.find_event_by_name(trace, "core.icf.iq_write")
        idr_align = self.find_event_by_name(trace, "core.idr.uop_decode", txid=201)
        self.assertTrue("parents" not in feq_acquire["args"])
        self.assertEqual(iq_write["args"]["parents"], "txid_190.")
        self.assertEqual(idr_align["args"]["parents"], "txid_198.txid_190.")

    def test_occupancy(self):
        trace = self.trace_events("--occupancy", "FEQ:core.bpu.feq.acquire,core.bpu.feq.release")
        feq_occupancy_thread = self.find(trace, name="thread_name", args={"name": "FEQ"})[0]
        self.assertTrue(feq_occupancy_thread)

    def test_splits(self):
        trace = self.trace_events("--split-all", "core.lsu.l1d_cache.access/hit=l1_hit{hit}")
        l1_hit0_thread = self.find(trace, name="thread_name", args={"name": "l1_hit0"})[0]
        l1_hit1_thread = self.find(trace, name="thread_name", args={"name": "l1_hit1"})[0]
        self.assertTrue(l1_hit0_thread)
        self.assertTrue(l1_hit1_thread)

    def test_split_renaming(self):
        trace = self.trace_events(
            "--split-all", "core.lsu.l1d_cache.access/hit=l1_hit{hit}",
            "--rename", "l1_hit(#)=\\1_l1_miss_hit",
        )  # fmt: skip
        old_l1_hit0_thread = self.find(trace, name="thread_name", args={"name": "l1_hit0"})
        old_l1_hit1_thread = self.find(trace, name="thread_name", args={"name": "l1_hit1"})
        new_l1_hit0_thread = self.find(trace, name="thread_name", args={"name": "0_l1_miss_hit"})[0]
        new_l1_hit1_thread = self.find(trace, name="thread_name", args={"name": "1_l1_miss_hit"})[0]
        self.assertEqual(len(old_l1_hit0_thread), 0)
        self.assertEqual(len(old_l1_hit1_thread), 0)
        self.assertTrue(new_l1_hit0_thread)
        self.assertTrue(new_l1_hit1_thread)

    def test_split_filtering(self):
        trace = self.trace_events(
            "--split", "core.lsu.l1d_cache.access/hit=l1_hit{hit}",
            "--rename", "l1_hit0=l1_miss",
            "--exclude", "l1_hit#",
        )  # fmt: skip
        old_l1_hit0_thread = self.find(trace, name="thread_name", args={"name": "l1_hit0"})
        old_l1_hit1_thread = self.find(trace, name="thread_name", args={"name": "l1_hit1"})
        new_l1_hit0_thread = self.find(trace, name="thread_name", args={"name": "l1_miss"})[0]
        self.assertEqual(len(old_l1_hit0_thread), 0)
        self.assertEqual(len(old_l1_hit1_thread), 0)
        self.assertTrue(new_l1_hit0_thread)

    @classmethod
    def keys_are_sorted(cls, event):
        keys = list(event["args"].keys())
        return keys == sorted(keys)

    def test_sorted(self):
        trace = self.trace_events("--event", "core.icf.iq_write")
        events = self.find_events_by_name(trace, "core.icf.iq_write")
        self.assertTrue(all(self.keys_are_sorted(e) for e in events))

    def test_no_sort(self):
        trace = self.trace_events("--no-sort-keys", "--event", "core.icf.iq_write")
        events = self.find_events_by_name(trace, "core.icf.iq_write")
        self.assertFalse(all(self.keys_are_sorted(e) for e in events))

    def test_default_hex(self):
        trace = self.trace_events("--event", "core.icf.iq_write")
        events = self.find_partial(trace, args={"program_counter"})
        all_hex = all(e["args"]["program_counter"].startswith("0x") for e in events)
        self.assertTrue(all_hex)

    def test_event_group(self):
        trace = self.trace_events(
            "--event", "core.icf.iq_write",
            "--event", "core.icf.feq_read",
            "--event", "core.idr.frontend_stall",
            "--event", "core.idr.rob.release",
            "-g", "core.idr",
            "-g", "core.icf",
        )  # fmt: skip

        idr_group = self.find(trace, name="process_name", args={"name": "core.idr"})
        self.assertTrue(idr_group)
        self.assertEqual(idr_group[0]["pid"], 1)
        self.assertEqual(len(self.find(trace, name="thread_name", pid=1)), 2)

        icf_group = self.find(trace, name="process_name", args={"name": "core.icf"})
        self.assertTrue(icf_group)
        self.assertEqual(icf_group[0]["pid"], 2)
        self.assertEqual(len(self.find(trace, name="thread_name", pid=2)), 2)

    def test_transaction_view(self):
        trace = self.trace_events(
            "--view", "transactions",
            "--tx_size", "128",
            "--tx_index", "core.idr.alloc_id",
            "--tx_start", "core.idr.uop_decode",
            "--tx_end", "core.gpc.rob_dealloc",
            "--tx_name", "core.idr.uop_disassembly",
            "--event", "core.idr.uop_dispatch",
            "--event", "core.ixu.uop_issue",
            "--event", "core.ixu.uop_data_available",
        )  # fmt: skip

        txs = self.find(trace, name="thread_name")
        self.assertTrue(all(tx["args"]["name"] == f"Entry {i:03}" for i, tx in enumerate(txs)))

        tx_start = self.find_partial(trace, args={"name": "core.idr.uop_decode"})
        tx_end = self.find_partial(trace, args={"name": "core.gpc.rob_dealloc"})
        self.assertGreaterEqual(len(tx_start), len(tx_end))

        tx_events = self.find_partial(trace, dur=lambda d: d > 333)
        self.assertEqual(len(tx_start), len(tx_events))
        self.assertTrue(all(isinstance(e["name"], str) and len(e["name"]) > 3 for e in tx_events))

        self.assertFalse(self.event_exists(trace, "start_transaction"))
        self.assertFalse(self.event_exists(trace, "end_transaction"))

    def test_period(self):
        trace = self.trace_events("--period", "400", "--event", "core.icf.iq_write")
        events = self.find(trace, tid=1, cat="none")
        self.assertTrue(all(e["dur"] == 400 for e in events))
