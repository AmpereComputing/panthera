# Copyright (c) 2024-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import argparse

import evp


class CBusySend:
    def __init__(self, output):
        super().__init__()

        self.output = open(output, "w")
        print(f"time,completer,rho,state,occupancy", file=self.output)

        evp.on("*.cbusy_send", lambda e: self._cbusy_send(e))
        evp.end_simulation(lambda: self.end_simulation())

    def end_simulation(self):
        self.output.close()

    def _cbusy_send(self, event):
        rho = event.data.get("rho", "")
        state = event.data.get("cbusy_state", "")
        occupancy = event.data.get("tracker_occupancy", "")
        component_name = event.name[: -len(".cbusy_send")]

        print(f"{event.time},{component_name},{rho},{state},{occupancy}", file=self.output)


parser = argparse.ArgumentParser()
parser.add_argument("--output", default="cbusy_send.csv")
args = parser.parse_args(evp_args)

CBusySend(args.output)
