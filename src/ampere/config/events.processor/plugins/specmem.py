# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""This is an EVP script. It count committed loads that reference data
fetched by speculatively executed uncommitted loads. Cache line
granularity. Data referenced by previously committed memops are not
counted if subsequently referenced speculatively."""

import collections

import evp

transactions = collections.defaultdict(list)

metrics = {}


def add_collect_metrics(trigger_time) -> dict:
    """callback from EVP for data we collect"""
    return metrics


def add_event_to_transaction(event):
    """callback from EVP for each event"""
    txid = event.data.get("txid", None)
    if txid:
        transactions[txid].append(event)


def print_transactions():
    """callback from EVP at end of run to summarize data"""
    decodes = 0
    commits = 0
    issues = 0
    loads = 0
    stores = 0
    uncommitted_refs = set()
    committed_refs = set()
    committed_loads = 0
    speculative_execution_prefetched = 0
    for txid in sorted(transactions.keys()):
        committed = False
        va = None
        load = False
        for e in transactions[txid]:
            if (
                (e.name == "sys.soc_0.core_0.idr.uop_dispatch" and "uopcode_disasm" in e.data)  # RTL
                or (e.name == "sys.soc_0.core_0.idr.uop_decode" and "uopcode_disasm" in e.data)
            ):  # PANTHERA
                decodes += 1
            elif e.name == "sys.soc_0.core_0.ixu.uop_issue":
                issues += 1
            elif e.name == "sys.soc_0.core_0.gpc.uop_commit":
                commits += 1
                committed = True
            elif e.name == "sys.soc_0.core_0.lsu.l1d_cache.access":
                if "va" in e.data:
                    va = e.data["va"]
                elif "virtual_address" in e.data:
                    va = e.data["virtual_address"]
                else:
                    print(f"ERROR: Missing 'va' & 'virtual_address': {e.data}")
                va = va & ~0x3F  # mask to line address
                loads += 1
                load = True
            elif e.name == "sys.soc_0.core_0.lsu.uop_sta_execute":
                # va = e.data['va']
                stores += 1
        if committed:
            if load:
                committed_loads += 1
                if va:
                    committed_refs.add(va)
                    if va in uncommitted_refs:
                        speculative_execution_prefetched += 1
                        uncommitted_refs.remove(va)
        else:  # not committed
            if va:
                if va not in committed_refs:  # avoid things referenced by prior committed loads
                    uncommitted_refs.add(va)

    def add_metric_datapoint(name: str, value: int):
        metrics[f"specmem.{name}"] = value

    def print_int(name: str, value: int):
        print(f"|{name:25} | {value:7}   |")
        add_metric_datapoint(name, value)

    def print_float(name: str, value: float):
        print(f"|{name:25} |    {value:7.2f}|")
        add_metric_datapoint(name, value)

    print_int("decodes", decodes)
    print_int("issues", issues)
    print_int("commits", commits)
    print_int("loads", loads)
    print_int("load_committed", committed_loads)
    print_int("load_speculative", loads - committed_loads)
    print_int("stores", stores)
    print_int("speculative_execution_prefetched", speculative_execution_prefetched)
    if committed_loads > 0:
        pct_speculative_execution_prefetched = 100.0 * speculative_execution_prefetched / committed_loads
    else:
        pct_speculative_execution_prefetched = 0
    print_float("pct_speculative_execution_prefetched", pct_speculative_execution_prefetched)

    pct_decode_no_commit = 100.0 * ((decodes - commits) / decodes)
    pct_decode_commit = 100.0 - pct_decode_no_commit

    pct_issue_no_commit = 100.0 * ((issues - commits) / issues)
    pct_issue_commit = 100.0 - pct_issue_no_commit

    print_float("pct_decode_no_commit", pct_decode_no_commit)
    print_float("pct_decode_commit", pct_decode_commit)
    print_float("pct_issue_no_commit", pct_issue_no_commit)
    print_float("pct_issue_commit", pct_issue_commit)


evp.collect(add_collect_metrics)

for event_name in [
    "*.idr.uop_dispatch",
    "*.idr.uop_decode",
    "*.ixu.uop_issue",
    "*.gpc.uop_commit",
    "*.lsu.l1d_cache.access",
    "*.lsu.uop_sta_execute",
]:
    evp.on(event_name, add_event_to_transaction)

evp.end_simulation(print_transactions)
