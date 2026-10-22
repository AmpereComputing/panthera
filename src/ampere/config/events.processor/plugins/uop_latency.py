# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import argparse
from collections import defaultdict

import evp
from plugins.common.transaction_tracker import TransactionTracker


class UopIssue(TransactionTracker):
    def __init__(self, source_pc, dest_pc):
        super().__init__()

        self.last_issue = 0
        self.issue_histogram = defaultdict(lambda: 0)

        if isinstance(source_pc, str):
            self.source_pc = int(source_pc, 0)
        else:
            self.source_pc = source_pc
        if isinstance(dest_pc, str):
            self.dest_pc = int(dest_pc, 0)
        else:
            self.dest_pc = dest_pc

        evp.on("*.ixu.uop_issue", lambda e: self._ixu_issue(e))
        evp.on("*.gpc.instruction_commit", lambda e: self._gpc_instruction_commit(e))
        evp.end_simulation(lambda: self.end_simulation())

    def print_histo(self, histodict):
        for histbin, occur in sorted(histodict.items()):
            print(f"{histbin}: {occur}")

    def end_simulation(self):
        heading_string = "Inter-issue delta ("
        if self.source_pc is not None:
            heading_string += f"from source PC {hex(self.source_pc)} to "
        if self.dest_pc is not None:
            heading_string += f"destination PC {hex(self.dest_pc)}):"
        else:
            heading_string += "any PC):"
        print(heading_string)
        self.print_histo(self.issue_histogram)

    def _ixu_issue(self, event):
        if event.data["txid"] in self.parents:
            parent_txid = self.parents[event.data["txid"]]
            self.add_tx_data(parent_txid, "issue_cycle", event.time)

    def _gpc_instruction_commit(self, event):
        ## upon commit compare each uop's issue time with previously recorded issue time
        if "commit_pc" in event.data:
            self.add_tx_data(event.data["txid"], "commit_pc", event.data["commit_pc"])
        if "inum" in event.data:
            self.add_tx_data(event.data["txid"], "inum", event.data["inum"])

        issue_cycle = self.get_tx_data(event.data["txid"], "issue_cycle")

        if issue_cycle is not None:
            if self.dest_pc is not None:
                if "commit_pc" in event.data and event.data["commit_pc"] == self.dest_pc:
                    self.issue_histogram[issue_cycle - self.last_issue] += 1
            else:
                self.issue_histogram[issue_cycle - self.last_issue] += 1

            if self.source_pc is not None:
                if "commit_pc" in event.data and event.data["commit_pc"] == self.source_pc:
                    self.last_issue = issue_cycle
            else:
                self.last_issue = issue_cycle


parser = argparse.ArgumentParser()
parser.add_argument("--source_pc")
parser.add_argument("--dest_pc")
args = parser.parse_args(evp_args)

UopIssue(args.source_pc, args.dest_pc)
