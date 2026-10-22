# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import re
from collections import defaultdict

import evp
from plugins.common.occupancy import Occupancy


class FbResidency:
    class Latency:
        def __init__(self):
            self.total_latency = 0
            self.total_count = 0

    def __init__(self, start_event, stop_event, output, name_col, latency_col="latency", rename=None, start_qual=None):
        self.tx = {}
        self.output = open(output, "w")
        self.rename = rename
        self.aggregated_latencies = defaultdict(lambda: FbResidency.Latency())
        self.start_qual = start_qual
        print(f"{name_col},{latency_col}", file=self.output)
        evp.on(start_event, lambda e: self._start(e))
        evp.on(stop_event, lambda e: self._stop(e))
        evp.collect(lambda trigger_time: self._collect())
        evp.end_simulation(lambda: self.output.close())

    def _start(self, event):
        if self.start_qual is None or self.start_qual(event):
            self.tx[event.data["txnid"]] = event.time

    def _stop(self, event):
        txid = event.data["txnid"]
        if txid in self.tx:
            latency = event.time - self.tx[txid]
            self._record(self._rename(event.name), latency)
            del self.tx[txid]

    def _record(self, name, latency):
        print(f"{name},{latency}", file=self.output)
        lat = self.aggregated_latencies[f"{name}_fb_residency"]
        lat.total_latency += latency
        lat.total_count += 1

    def _rename(self, event_name):
        return self.rename(event_name) if self.rename is not None else event_name

    def _collect(self):
        return {inj: lat.total_latency / lat.total_count / 1000.0 for inj, lat in self.aggregated_latencies.items()}


###################################################################################

name_re = re.compile(r"sys\.soc_0\.core_([0-9]+)")


def short_name(event_name):
    m = name_re.match(event_name)
    return f"inj{m.group(1)}"


FbResidency(
    f"sys.soc_0.core_#.txn_id_alloc", f"sys.soc_0.core_#.txn_id_dealloc", "fb_residency.csv", "inj", rename=short_name
)


def core_name(event_name):
    return event_name.rsplit(".", 1)[0]


Occupancy(
    "*.core_#.txn_id_alloc", "*.core_#.txn_id_dealloc", "fb_occupancy.csv", "core", "fb occupancy", rename=core_name
)
