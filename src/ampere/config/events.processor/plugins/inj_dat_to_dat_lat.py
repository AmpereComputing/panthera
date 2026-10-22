# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import re
from collections import defaultdict

import evp


class Inj2InjLatency:
    class Latency:
        def __init__(self):
            self.total_latency = 0
            self.total_count = 0

    def __init__(self, start_event, stop_event, output, name_col, latency_col="latency", rename=None):
        self.dict = {}
        self.output = open(output, "w")
        self.rename = rename
        self.aggregated_latencies = defaultdict(lambda: Inj2InjLatency.Latency())
        print(f"{name_col},{latency_col}", file=self.output)
        evp.on(start_event, lambda e: self._start(e))
        evp.on(stop_event, lambda e: self._stop(e))
        evp.collect(lambda trigger_time: self._collect())
        evp.end_simulation(lambda: self.output.close())

    def _start(self, event):
        txid = event.data["txid"]
        srcid = event.data["srcid"]
        self.dict[f"{txid}{srcid}"] = event.time

    def _stop(self, event):
        txid = event.data["txid"]
        srcid = event.data["srcid"]
        key = f"{txid}{srcid}"
        if key in self.dict:
            latency = event.time - self.dict[key]
            self._record(self._rename(event.name), latency)
            del self.dict[key]

    def _record(self, name, latency):
        print(f"{name},{latency}", file=self.output)
        lat = self.aggregated_latencies[f"{name}_latency"]
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


Inj2InjLatency(
    f"sys.soc_0.core_#.send_dat", f"sys.soc_0.core_#.recv_dat", "inj2inj_latency.csv", "inj", rename=short_name
)
