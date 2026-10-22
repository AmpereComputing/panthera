# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from collections import defaultdict

import evp


class IOLatency:
    class AggrLat:
        def __init__(self):
            self.total_latency = 0
            self.total_count = 0

    def __init__(
        self, start_event, stop_event, output, name_col, latency_col="latency", rename=None, start_qual=None, avg=False
    ):
        self.tx = {}
        self.output = open(output, "w")
        self.rename = rename
        self.start_qual = start_qual
        self.total_latency = 0
        self.total_count = 0
        self.name = name_col
        self.avg = avg
        self.aggregated_latencies = defaultdict(lambda: IOLatency.AggrLat())
        print(f"{name_col},{latency_col}", file=self.output)
        evp.on(start_event, lambda e: self._start(e))
        evp.on(stop_event, lambda e: self._stop(e))
        evp.collect(lambda trigger_time: self._collect())
        if avg is False:
            evp.end_simulation(lambda: self.output.close())
        else:
            evp.end_simulation(lambda: self._dump())

    def _start(self, event):
        if self.start_qual is None or self.start_qual(event):
            if self.tx.get(event.data["io_id"]) is None:
                self.tx[event.data["io_id"]] = event.time

    def _stop(self, event):
        txid = event.data["io_id"]
        if txid in self.tx:
            latency = event.time - self.tx[txid]
            self.total_latency += latency
            self.total_count += 1
            self._record(self._rename(event.name), latency)
            del self.tx[txid]

    def _record(self, name, latency):
        if self.avg is False:
            print(f"{name},{latency}", file=self.output)
        lat = self.aggregated_latencies[f"{name}_latency"]
        lat.total_latency += latency
        lat.total_count += 1

    def _rename(self, event_name):
        return self.rename(event_name) if self.rename is not None else event_name

    def _collect(self):
        return {inj: lat.total_latency / lat.total_count / 1000.0 for inj, lat in self.aggregated_latencies.items()}

    def _dump(self):
        avg_latency = self.total_latency / self.total_count / 1000.0 if self.total_count != 0 else 0
        print(f"average {self.name}: {avg_latency}", file=self.output)
        self.output.close()
