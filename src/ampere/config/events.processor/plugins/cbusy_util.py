# Copyright (c) 2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import argparse

import evp


class CBusyUtilization:
    def __init__(self, output):
        super().__init__()

        self.output = open(output, "w")
        print(f"time,completer,rho,phase,allocates,deallocates", file=self.output)

        evp.on("*.snapshot_rho", lambda e: self._cbusy_util(e))
        evp.end_simulation(lambda: self.end_simulation())

    def end_simulation(self):
        self.output.close()

    def _cbusy_util(self, event):
        rho = event.data.get("rho", "")
        phase = event.data.get("phase", "")
        allocates = event.data.get("allocates", "")
        deallocates = event.data.get("deallocates", "")
        component_name = event.name[: -len(".snapshot_rho")]

        print(f"{event.time},{component_name},{rho},{phase},{allocates},{deallocates}", file=self.output)


parser = argparse.ArgumentParser()
parser.add_argument("--output", default="cbusy_util.csv")
args = parser.parse_args(evp_args)

CBusyUtilization(args.output)
