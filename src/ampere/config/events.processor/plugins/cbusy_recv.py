# Copyright (c) 2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import argparse

import evp


class CBusyRecv:
    def __init__(self, output):
        super().__init__()

        self.output = open(output, "w")
        print(f"time,requester,limit,outstanding,acc,cbusy_idx", file=self.output)

        evp.on("*.cbusy.update", lambda e: self._cbusy_recv(e))
        evp.end_simulation(lambda: self.end_simulation())

    def end_simulation(self):
        self.output.close()

    def _cbusy_recv(self, event):
        limit = event.data.get("max_outstanding_requests", "")
        outstanding = event.data.get("outstanding_requests", "")
        acc = event.data.get("acc_cntr", "")
        cbusy_idx = event.data.get("cbusy_idx", "")
        component_name = event.name[: -len(".cbusy.update")]

        print(f"{event.time},{component_name},{limit},{outstanding},{acc},{cbusy_idx}", file=self.output)


parser = argparse.ArgumentParser()
parser.add_argument("--output", default="cbusy_recv.csv")
args = parser.parse_args(evp_args)

CBusyRecv(args.output)
