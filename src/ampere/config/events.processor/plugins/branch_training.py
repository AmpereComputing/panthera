# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import argparse

import evp
from plugins.common.transaction_tracker import TransactionTracker


class BranchTraining(TransactionTracker):
    def __init__(self, branch_pcs=[]):
        super().__init__()
        self.branch_pcs = branch_pcs
        evp.on("*.gpc.instruction_commit", lambda e: self._gpc_instruction_commit(e))
        evp.on("*.bpu.br_train", lambda e: self.on_br_train(e))

    def _gpc_instruction_commit(self, event):
        if "inum" in event.data:
            self.add_tx_data(event.data["txid"], "inum", event.data["inum"])

    def on_br_train(self, event):
        pc_name = "program_counter" if "program_counter" in event.data else "pc"
        br_pc = event.data[pc_name]

        if len(self.branch_pcs) > 0 and br_pc not in self.branch_pcs:
            return

        inum = self.get_tx_data(event.data["txid"], "inum", search_parents=True)

        br_target = event.data["br_target"]
        taken = event.data["br_taken"] == 1
        mispredicted = event.data["br_mispredicted"] == 1
        use_alternate = event.data["br_use_alternate"] == 1
        primary_predictor = event.data["br_primary_predictor"].split(".")[-1]
        alternate_predictor = event.data["br_alternate_predictor"].split(".")[-1]

        t_nt = "NT" if not taken else "T"
        mis_string = "MIS" if mispredicted else ""
        print(
            f"br_train txid_{event.data['txid']}. inum_{inum}. PC: 0x{br_pc: >16x} {t_nt: >2} {mis_string: >3} {primary_predictor: >10} {alternate_predictor: >10}"
        )


def any_base_int(i):
    return int(i, 0)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--branch-pc",
        type=any_base_int,
        action="append",
        default=[],
        help="The PC of the branches to track (defaults to all branches if not supplied",
    )
    args = parser.parse_args(evp_args)
    BranchTraining(args.branch_pc)


main()
