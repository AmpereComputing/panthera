# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import collections
import re

import evp

transactions = collections.defaultdict(list)


def add_event_to_transaction(event):
    txid = event.data.get("txid", None)

    if txid:
        transactions[txid].append(event)


def print_transactions():
    histogram = collections.Counter()
    last_div_commit = None
    for txid in sorted(transactions.keys()):
        # print(f'txid {txid}:')
        div = None
        for e in transactions[txid]:
            if (e.name == "sys.soc_0.core_0.idr.uop_dispatch" and "uopcode_disasm" in e.data) or (
                e.name == "sys.soc_0.core_0.idr.uop_decode" and "uopcode_disasm" in e.data
            ):
                udis = e.data["uopcode_disasm"]
                # print(f'  {e.time} {e.name} {udis}')
                if re.search(r"div", udis, re.IGNORECASE):
                    div = udis
                else:
                    div = None
            if div and e.name == "sys.soc_0.core_0.gpc.uop_commit":
                print(f"  {e.time} {div} DIV-COMMIT", end="")
                if last_div_commit:
                    delta = (
                        e.time - last_div_commit
                    ) / 333  # for panthera or 400 for RTL FIXME make  a parameter if evp supports that
                    print(f" {delta}")
                    histogram[delta] += 1
                else:
                    print()
                last_div_commit = e.time
    for delta in sorted(histogram.keys()):
        print(f"HISTOGRAM {delta} {histogram[delta]}")


for event_name in ["*.idr.uop_dispatch", "*.idr.uop_decode", "*.gpc.uop_commit"]:
    evp.on(event_name, add_event_to_transaction)


evp.end_simulation(print_transactions)
