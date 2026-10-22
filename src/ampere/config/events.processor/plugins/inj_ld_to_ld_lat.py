# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Measure load to load latency on a mesh injector.

This is very similar to req_to_req latency, which is measure from one CHI
request to the next, except this uses the `load_exec` event. This gives
more reasonable answers when loads hit the L2 cache.
"""

import argparse

import evp

parser = argparse.ArgumentParser()
parser.add_argument("--latency-core", default=0, help="The core that is running latency thread")
parser.add_argument("--output", help="Output file for per-transaction latencies")
args = parser.parse_args(evp_args)
lat_core = args.latency_core


class Ld2LdLatency:
    def __init__(self, event, name, output=None):
        self.start_time = None
        self.last_txid = None
        self.total_latency = 0
        self.total_count = 0
        self.name = name

        evp.on(event, lambda e: self._start(e))
        evp.collect(lambda trigger_time: self._collect())

        if output:
            self.output = open(output, "w")
            print("txid,start_time,end_time,latency", file=self.output)
            evp.end_simulation(lambda: self.output.close())
        else:
            self.output = None

    def _start(self, event):
        txid = event.data["txid"]
        if txid != self.last_txid:
            if self.start_time is not None:
                self._record(self.last_txid, self.start_time, event.time)
            self.start_time = event.time
            self.last_txid = event.data["txid"]

    def _record(self, txid, start_time, end_time):
        latency = end_time - start_time
        if self.output:
            print(f"{txid},{start_time},{end_time},{latency / 1000.0}", file=self.output)
        self.total_latency += latency
        self.total_count += 1

    def _collect(self):
        if self.total_count:
            return {self.name: self.total_latency / self.total_count / 1000.0}
        else:
            return {self.name: 0}


###################################################################################

Ld2LdLatency(f"sys.soc_0.core_{lat_core}.load_exec", f"inj{lat_core}_latency", args.output)
