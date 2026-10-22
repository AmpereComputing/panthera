# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import re
import sys
from collections import defaultdict

import evp
import panthera.enums.chi_opcodes


class Inj2InjBW:
    class BW:
        def __init__(self):
            self.total_bw = 0
            self.total_count = 0

    def __init__(self, refill_event, barrier_event, output, name_col, bw_col="bw", rename=None):
        # self.dict = {}
        self.output = open(output, "w")
        self.rename = rename
        self.time_stamp = None
        self.packets = 0
        self.aggregated_bw = defaultdict(lambda: Inj2InjBW.BW())
        print(f"{name_col},{bw_col}", file=self.output)
        evp.on(refill_event, lambda e: self._count(e))
        evp.on(barrier_event, lambda e: self._calc_bw(e))
        evp.collect(lambda trigger_time: self._collect())
        evp.end_simulation(lambda: self.output.close())

    def _count(self, event):
        if self.time_stamp is not None:
            self.packets += 1

    def _calc_bw(self, event):
        if self.time_stamp is not None:
            bw = 64 * (self.packets / ((event.time - self.time_stamp) / 1000))
            self._record(self._rename(event.name), bw)
            self.time_stamp = event.time
            self.packets = 0
        else:
            self.time_stamp = event.time

    def _record(self, name, bw):
        print(f"{name},{bw}", file=self.output)
        lat = self.aggregated_bw[f"{name}_bw"]
        lat.total_bw += bw
        lat.total_count += 1

    def _rename(self, event_name):
        return self.rename(event_name) if self.rename is not None else event_name

    def _collect(self):
        return {inj: bw.total_bw / bw.total_count for inj, bw in self.aggregated_bw.items()}


###################################################################################

name_re = re.compile(r"sys\.soc_0\.core_([0-9]+)")


def short_name(event_name):
    m = name_re.match(event_name)
    return f"inj{m.group(1)}"


Inj2InjBW(
    f"sys.soc_0.core_#.cache.refill", f"sys.soc_0.core_#.barrier_comp", "inj2inj_bw.csv", "inj", rename=short_name
)
