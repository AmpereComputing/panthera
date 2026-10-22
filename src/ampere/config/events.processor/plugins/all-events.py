# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Print out all events associated with a transaction from an evp stream"""

import collections
import sys

import adis
import evp

transactions = collections.defaultdict(list)


class notes:
    """Summary event with less data than full even stream"""

    def __init__(self, event):
        self.time = event.time
        self.name = event.name


def add_event_to_transaction(event):
    """Collect event info while consuming the event stream"""
    txid = event.data.get("txid", None)
    if txid:
        note = notes(event)
        # note.data = event.data
        # check all for these...
        for dname in [
            "pa",
            "physical_address",
            "sys.soc_0.core_0.l2c.read_pipeline.pa",
            "sys.soc_0.core_0.l2c.prefetcher.pa",
            "sys.soc_0.core_0.l2c.write_pipeline.pa",
            "sys.soc_0.core_0.lsu.il1tlb.pa",
            "sys.soc_0.core_0.icf.pa",
            "sys.soc_0.core_0.lsu.pa",
            "sys.soc_0.core_0.lsu.dl1tlb.pa",
        ]:
            if dname in event.data:
                note.pa = event.data[dname]
                break

        for dname in [
            "va",
            "virtual_address",
            "sys.soc_0.core_0.lsu.il1tlb.va",
            "sys.soc_0.core_0.lsu.l1d_cache.va",
            "sys.soc_0.core_0.lsu.va",
            "sys.soc_0.core_0.dmmu.va",
            "sys.soc_0.core_0.lsu.dl1tlb.va",
        ]:
            if dname in event.data:
                note.va = event.data[dname]
                break

        for dname in ["pc", "program_counter", "commit_pc"]:
            if dname in event.data:
                note.pc = event.data[dname]
                break
        for dname in ["inst_bytes"]:
            if dname in event.data:
                note.itext = event.data[dname]
                break
        for dname in ["inum", "gpc.inum"]:
            if dname in event.data:
                note.inum = event.data[dname]
                break

        if event.name in ["sys.soc_0.core_0.icf.fetch", "sys.soc_0.core_0.idr.uop_decode"]:
            if "instruction" in event.data:
                note.itext = event.data["instruction"]
        if event.name in ["sys.soc_0.core_0.idr.uop_dispatch", "sys.soc_0.core_0.idr.uop_decode"]:
            if "uopcode_disasm" in event.data:
                note.udis = event.data["uopcode_disasm"]
        elif event.name == "sys.soc_0.core_0.ixu.uop_issue":
            if "ixu_pipe" in event.data:
                note.ixu_pipe = int(event.data["ixu_pipe"])
        elif event.name == "sys.soc_0.core_0.lsu.dl1tlb.access":
            note.hit = int(event.data["hit"])
            if "pcp" in event.data:
                note.pcp = event.data["pcp"]
            if "prefetch" in event.data:
                note.prefetch = event.data["prefetch"]
        elif event.name in ["sys.soc_0.core_0.lsu.l1d_cache.access"]:
            note.hit = int(event.data["hit"])
        elif event.name in ["sys.soc_0.core_0.lsu.uop_ld_recycle"]:
            note.ld_recycle_reason = int(event.data["ld_recycle_reason"])
            note.ld_wakeup_code = int(event.data["ld_wakeup_code"])
        elif event.name == "sys.soc_0.core_0.gpc.instruction_commit":
            note.inum = int(event.data["sys.soc_0.core_0.gpc.inum"])
        elif event.name == "start_transaction":
            if "parent" in event.data:
                note.parent = int(event.data["parent"])
        # note.data = event.data
        transactions[txid].append(note)


def print_transactions():
    """Print results of collection at end of event stream processing"""
    for txid in sorted(transactions.keys()):
        print(f"txid {txid}:")
        for note in transactions[txid]:
            s = [f"  {note.time:9} {note.name}"]
            if hasattr(note, "parent"):
                s.append(f"parent: {note.parent}")
            if hasattr(note, "itext"):
                dis = adis.dis(note.itext)
                s.append(f"ITEXT: {note.itext:08x} AINST: {dis}")
            if hasattr(note, "udis"):
                s.append(f"{note.udis}")
            if hasattr(note, "pc"):
                s.append(f"PC: {note.pc:016x}")
            if hasattr(note, "va"):
                s.append(f"VA: {note.va:016x}")
            if hasattr(note, "pa"):
                s.append(f"PA: {note.pa:016x}")
            if hasattr(note, "hit"):
                if note.hit:
                    s.append("HIT")
                else:
                    s.append("MISS")
            if hasattr(note, "pcp"):
                s.append(f"PCP: {note.pcp}")
            if hasattr(note, "inum"):
                s.append(f"inum: {note.inum}")
            if hasattr(note, "ixu_pipe"):
                s.append(f"ixu_pipe: {note.ixu_pipe}")
            if hasattr(note, "prefetch"):
                s.append(f"PREFETCH: {note.prefetch}")
            if hasattr(note, "ld_recycle_reason"):
                s.append(f"ld_recycle_reason: {note.ld_recycle_reason}")
            if hasattr(note, "ld_wakeup_code"):
                s.append(f"ld_wakeup_code: {note.ld_wakeup_code}")
            if hasattr(note, "data"):
                s.append(f"data: {note.data}")
            print(" ".join(s))


evp.on("*", add_event_to_transaction)
evp.end_simulation(print_transactions)
