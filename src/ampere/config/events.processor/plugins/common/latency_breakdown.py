# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import csv
from copy import copy
from dataclasses import dataclass
from re import match

import evp
import panthera.enums.chi_opcodes


class LatencyBreakdown:
    @dataclass
    class TransactionData:
        name: None
        time: None

    def __init__(self, events, start_event, stop_event, rename_rules, output):
        self.output = output
        self.start_event = start_event
        self.stop_event = stop_event
        self.rename_rules = rename_rules
        self.inflight = {}
        self.latencies = {}
        self.events = {}

        # Register events
        evp.end_simulation(lambda: self.end_simulation())
        for event in events:
            if type(event) is tuple:
                self.events[self._glob_to_regex(event[0])] = event[1]
                evp.on(event[0], lambda e: self._on_event(e))
            else:
                evp.on(event, lambda e: self._on_event(e))

    def _on_event(self, event):
        txid = event.data["txid"]
        transaction = self.TransactionData(event.name, event.time)

        # Filter events by opcode if requested
        for ev_pattern, filter_func in self.events.items():
            if match(ev_pattern, event.name):
                if not filter_func(event):
                    return

        # Start of latency measurement. we start tracking a new in flight transactions
        if self.start_event == transaction.name:
            self.inflight[txid] = []

        # If we received an event that has the same txid we know its related and
        # we add event to a transaction array for this txid
        if txid in self.inflight:
            self.inflight[txid].append(transaction)

        # Stop of latency measurement, the last type of event we expect to receive
        # when determining the latency
        if self.stop_event == transaction.name and txid in self.inflight:
            # calculate/determine the latency of this subcomponent of the transaction
            self._calc_component_latencies(self.inflight[txid])

            # Reclassify transaction groups from src->dst to human readable names
            transactions = self._classify_transactions(self.inflight[txid], self.rename_rules)

            # Track/sum the individual latencies within the transactions group
            self._sum_latencies(transactions)
            del self.inflight[txid]

    # I am sure there is something better to translate globs to regexs
    def _glob_to_regex(self, glob):
        return glob.replace("*", ".*").replace("#", "\d+")

    def _calc_lat_avg(self, transactions):
        rc = {}
        for name, val in transactions.items():
            rc[name] = round((val[0] / val[1] / 1000), 2)
        return rc

    def _sum_latencies(self, transactions):
        for txn in transactions:
            if txn.name not in self.latencies:
                self.latencies[txn.name] = (0, 0)  # Total Time, Total transactions

            latency, count = self.latencies[txn.name]
            latency += txn.time
            count += 1
            self.latencies[txn.name] = (latency, count)

    # Rename transactions depending on regex rules of src->tgt
    def _classify_transactions(self, transactions, rename_rules):
        prev_txn = None
        message_breakdown = []

        for txn in transactions:
            if prev_txn is None:
                prev_txn = txn
            else:
                match_found = False
                for category, patterns in rename_rules:
                    if match(patterns[0], prev_txn.name) and match(patterns[1], txn.name):
                        element = copy(txn)
                        element.name = category
                        message_breakdown.append(element)
                        match_found = True
                        break
                if not match_found:
                    element = copy(txn)
                    message_breakdown.append(element)

                prev_txn = txn
        return message_breakdown

    def _calc_component_latencies(self, transactions):
        prev_time = None
        for txn in list(transactions):
            if prev_time is None:
                prev_time = txn.time
                txn.time = 0
            else:
                temp_time = txn.time
                txn.time -= prev_time
                prev_time = temp_time

    def end_simulation(self):
        lat_breakdown = self._calc_lat_avg(self.latencies)
        with open(self.output, "w") as fd:
            w = csv.DictWriter(fd, lat_breakdown.keys())
            w.writeheader()
            w.writerow(lat_breakdown)
