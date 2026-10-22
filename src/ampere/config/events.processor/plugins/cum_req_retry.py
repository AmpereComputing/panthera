# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import re
import sys
from collections import defaultdict

import evp


class CumulativeTimeSeries:
    def __init__(self, event, predicate=None, mapper=None, output=None):
        self._mapper = mapper
        self._occurrences = defaultdict(lambda: 0)
        self._produced_header = False

        if output:
            self._output = open(output, "w")
        else:
            self._output = sys.stdout

        def maybe_count(e):
            if predicate(e):
                self._happening(e)

        if predicate:
            evp.on(event, maybe_count)
        else:
            evp.on(event, lambda e: self._happening(e))

    def _happening(self, event):
        if self._mapper:
            meta = self._mapper(event)
            self._occurrences[meta] += 1
            self._record(event.time, self._occurrences[meta], meta)
        else:
            self._occurrences["count"] += 1
            self._record(event.time, self._occurrences["count"])

    def _record(self, time, count, meta=()):
        if not self._produced_header:
            self._print_header(meta)
        values = [time] + [m[1] for m in meta] + [count]
        print(*values, sep=",", file=self._output)

    def _print_header(self, meta):
        cols = ["time"] + [m[0] for m in meta] + ["count"]
        print(*cols, sep=",", file=self._output)
        self._produced_header = True


def hnf_name(event):
    m = re.search("homenode_([0-9]+)", event.name)
    return (("hnf", f"hnf{m.group(1)}"),)


def is_retry(event):
    return event.data["chi_rsp_opcode"] == 3


CumulativeTimeSeries("*.homenode_#.recv_req", mapper=hnf_name, output=f"{output_dir}/hnf_reqs.csv")
CumulativeTimeSeries(
    "*.homenode_#.send_rsp", predicate=is_retry, mapper=hnf_name, output=f"{output_dir}/hnf_retries.csv"
)
