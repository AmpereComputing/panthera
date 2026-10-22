# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import argparse
from collections import Counter

import evp
from plugins.common.transaction_tracker import TransactionTracker


class BranchMispredictions(TransactionTracker):
    def __init__(self, output, max_pcs=20):
        super().__init__(on_end_tx=lambda txid: self._check_for_misprediction(txid))

        self.mispredicted_pcs = Counter()
        self.committed_pcs = Counter()

        self.output = open(output, "w")
        self.num_misprediction_pcs = max_pcs

        evp.on("*.gpc.flush", lambda e: self._gpc_flush(e))
        evp.on("*.gpc.instruction_commit", lambda e: self._gpc_instruction_commit(e))
        evp.end_simulation(lambda: self.end_simulation())

    def end_simulation(self):
        self.output.close()

        sorted_mispredicted_pcs = self.mispredicted_pcs.most_common(self.num_misprediction_pcs)
        total_mispredictions = self.mispredicted_pcs.total()
        total_instructions = self.committed_pcs.total()
        print(
            f"Found {total_mispredictions} mispredicted branches over {total_instructions} committed instructions ({1000.0 * total_mispredictions / total_instructions:.3f} MPKI)"
        )
        print(f"Top {self.num_misprediction_pcs} mispredictions, by branch PC:")
        for pc, mispredicted_count in sorted_mispredicted_pcs:
            print(
                f"PC: 0x{pc: >16x}, mispredicted {mispredicted_count} times ({mispredicted_count / self.committed_pcs[pc]:.2%} of {self.committed_pcs[pc]} commits)"
            )

    def _gpc_flush(self, event):
        # Flush type 9 is a branch misprediction
        if event.data["flush_type"] == 9:
            self.add_tx_data(event.data["txid"], "branch_misprediction", True)

    def _gpc_instruction_commit(self, event):
        if "commit_pc" in event.data:
            commit_pc = event.data["commit_pc"]
            self.add_tx_data(event.data["txid"], "commit_pc", commit_pc)
            self.committed_pcs[commit_pc] += 1
        if "inum" in event.data:
            self.add_tx_data(event.data["txid"], "inum", event.data["inum"])

    def _check_for_misprediction(self, txid):
        branch_misprediction = self.get_tx_data(txid, "branch_misprediction")
        if branch_misprediction:
            self._handle_misprediction(txid)

    def _handle_misprediction(self, txid):
        commit_pc = self.get_tx_data(txid, "commit_pc", search_parents=True)
        inum = self.get_tx_data(txid, "inum", search_parents=True)
        if commit_pc is None or inum is None:
            #            self.output.write(f'skipping misprediction at txid:{txid} without commit information, probably speculative\n')
            return
        self.output.write(f"mispredicted PC:0x{commit_pc:02x} inum:{inum}\n")
        self.mispredicted_pcs[commit_pc] += 1


parser = argparse.ArgumentParser()
parser.add_argument("--filename", default="branch_mispredictions.log")
parser.add_argument("--max-pcs", type=int, default=20)
args = parser.parse_args(evp_args)

BranchMispredictions(args.filename, args.max_pcs)
