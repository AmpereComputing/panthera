# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import argparse

import evp
from panthera.enums.chi_opcodes import DataOpcode, RequestOpcode, ResponseOpcode, SnoopOpcode
from plugins.common.transaction_tracker import TransactionTracker


class TxnPiece:
    def get_channel_opcode(self, event_name, ev_data):
        if "_req" in event_name:
            return ("req", RequestOpcode(ev_data["chi_req_opcode"]))
        if "_rsp" in event_name:
            return ("rsp", ResponseOpcode(ev_data["chi_rsp_opcode"]))
        if "_snp" in event_name:
            return ("snp", SnoopOpcode(ev_data["chi_snp_opcode"]))
        if "_dat" in event_name:
            return ("dat", DataOpcode(ev_data["chi_dat_opcode"]))
        raise Exception("Unknown Channel")

    def __init__(self, ev_name, ev_data, ev_time):
        self.channel, self.opcode = self.get_channel_opcode(ev_name, ev_data)
        self.start_ts = ev_time / 1000
        self.end_ts = -1
        self.srcid = ev_data["srcid"]
        self.tgtid = ev_data["tgtid"]
        self.txid = ev_data["txid"]

    def __eq__(self, other):
        if not isinstance(other, TxnPiece):
            return NotImplemented

        if (
            self.channel == other.channel
            and self.opcode == other.opcode
            and self.srcid == other.srcid
            and self.tgtid == other.tgtid
            and self.txid == other.txid
        ):
            return True
        else:
            return False

    def __str__(self):
        return f"{self.opcode}({self.srcid}->{self.tgtid}, {self.start_ts}ns->{self.end_ts}ns)"


class LatencyFlow(TransactionTracker):
    paths = {}
    start_transaction = None

    def __init__(self, output, start_transaction):
        self.output = open(output, "w")
        super().__init__(on_end_tx=self.end_tx)
        if start_transaction is not None:
            for req in RequestOpcode:
                if str(req) == start_transaction:
                    start_transaction = req
                    self.start_transaction = RequestOpcode(start_transaction)
                    break

        evp.on("*.core_#.send_req", lambda e: self.start_txn_piece(e, "core_send_req"))
        evp.on("*.pxha_#.send_req", lambda e: self.start_txn_piece(e, "pxha_send_req"))
        evp.on("*.pmha_#.send_req", lambda e: self.start_txn_piece(e, "pmha_send_req"))

        evp.on("*.core_#.send_rsp", lambda e: self.start_txn_piece(e, "core_send_rsp"))
        evp.on("*.pxha_#.send_rsp", lambda e: self.start_txn_piece(e, "pxha_send_rsp"))
        evp.on("*.pmha_#.send_rsp", lambda e: self.start_txn_piece(e, "pmha_send_rsp"))
        evp.on("*.cmc_#.send_rsp", lambda e: self.start_txn_piece(e, "mem_send_rsp"))
        evp.on("*.memory_#.send_rsp", lambda e: self.start_txn_piece(e, "mem_send_rsp"))

        evp.on("*.core_#.send_dat", lambda e: self.start_txn_piece(e, "core_send_dat"))
        evp.on("*.pxha_#.send_dat", lambda e: self.start_txn_piece(e, "pxha_send_dat"))
        evp.on("*.pmha_#.send_dat", lambda e: self.start_txn_piece(e, "pmha_send_dat"))
        evp.on("*.cmc_#.send_dat", lambda e: self.start_txn_piece(e, "mem_send_dat"))
        evp.on("*.memory_#.send_dat", lambda e: self.start_txn_piece(e, "mem_send_dat"))

        evp.on("*.core_#.recv_req", lambda e: self.end_txn_piece(e, "core_recv_req"))
        evp.on("*.pxha_#.recv_req", lambda e: self.end_txn_piece(e, "pxha_recv_req"))
        evp.on("*.pmha_#.recv_req", lambda e: self.end_txn_piece(e, "pmha_recv_req"))
        evp.on("*.cmc_#.recv_req", lambda e: self.end_txn_piece(e, "mem_recv_req"))
        evp.on("*.memory_#.recv_req", lambda e: self.end_txn_piece(e, "mem_recv_req"))

        evp.on("*.core_#.recv_rsp", lambda e: self.end_txn_piece(e, "core_recv_rsp"))
        evp.on("*.pxha_#.recv_rsp", lambda e: self.end_txn_piece(e, "pxha_recv_rsp"))
        evp.on("*.pmha_#.recv_rsp", lambda e: self.end_txn_piece(e, "pmha_recv_rsp"))
        evp.on("*.cmc_#.recv_rsp", lambda e: self.end_txn_piece(e, "mem_recv_rsp"))
        evp.on("*.memory_#.recv_rsp", lambda e: self.end_txn_piece(e, "mem_recv_rsp"))

        evp.on("*.core_#.recv_dat", lambda e: self.end_txn_piece(e, "core_recv_dat"))
        evp.on("*.pxha_#.recv_dat", lambda e: self.end_txn_piece(e, "pxha_recv_dat"))
        evp.on("*.pmha_#.recv_dat", lambda e: self.end_txn_piece(e, "pmha_recv_dat"))
        evp.on("*.cmc_#.recv_dat", lambda e: self.end_txn_piece(e, "mem_recv_dat"))
        evp.on("*.memory_#.recv_dat", lambda e: self.end_txn_piece(e, "mem_recv_dat"))

        evp.end_simulation(lambda: self.end_simulation())

    def end_tx(self, txid):
        # This is a way to only capture when the parent transaction is cleaned up
        physical_address = self.get_tx_data(txid, "physical_address", search_parents=False)
        self.normalize(txid)
        if physical_address:
            transaction_seq = self.get_tx_data(txid, "transaction_seq", search_parents=True)
            pxha = transaction_seq[0].tgtid
            if pxha in self.paths:
                self.paths[pxha].append(transaction_seq)
            else:
                self.paths[pxha] = [transaction_seq]

    def normalize(self, txid):
        transaction_seq = self.get_tx_data(txid, "transaction_seq", search_parents=True)
        if transaction_seq is not None:  # We are only recording ReadShared starts, ignoring writeevictorevict
            start_time = transaction_seq[0].start_ts
            for t in transaction_seq:
                t.start_ts -= start_time
                t.end_ts -= start_time

    def print(self, txid):
        transaction_seq = self.get_tx_data(txid, "transaction_seq", search_parents=True)
        physical_address = self.get_tx_data(txid, "physical_address", search_parents=True)
        if transaction_seq is not None:  # We are only recording ReadShared starts, ignoring writeevictorevict
            print(f"0x{physical_address:x}: ", end="", file=self.output)
            for t in transaction_seq:
                print(f"{t},", end="", file=self.output)
            print("", file=self.output)

    def start_txn_piece(self, event, name):
        txid = event.data["txid"]
        piece = TxnPiece(name, event.data, event.time)

        # Start of transaction for 1r0w starts with ReadShared from the core
        if name == "core_send_req":
            physical_address = event.data["physical_address"]
            opcode = RequestOpcode(event.data["chi_req_opcode"])

            if self.start_transaction is None or opcode == self.start_transaction:
                self.add_tx_data(txid, "transaction_seq", [piece])
                self.add_tx_data(txid, "physical_address", physical_address)

        # All other send transaction pieces
        else:
            transaction_seq = self.get_tx_data(txid, "transaction_seq", search_parents=True)
            if transaction_seq is not None:  # We are only recording ReadShared starts, ignoring writeevictorevict
                transaction_seq.append(piece)

    def end_txn_piece(self, event, name):
        txid = event.data["txid"]
        transaction_seq = self.get_tx_data(txid, "transaction_seq", search_parents=True)

        if transaction_seq is not None:  # We are only recording ReadShared starts, ignoring writeevictorevict
            piece = TxnPiece(name, event.data, event.time)
            for txn_piece in transaction_seq:
                if piece == txn_piece:
                    txn_piece.end_ts = piece.start_ts
                    return

    def end_simulation(self):
        for pxha in sorted(self.paths.keys()):
            for index, path in enumerate(self.paths[pxha]):
                if index == 0:
                    continue
                for transactions_seq in path:
                    print(f"{transactions_seq}", end=", ", file=self.output)
                print("", file=self.output)
        self.output.close()


parser = argparse.ArgumentParser()
parser.add_argument(
    "--start-transaction",
    type=str,
    default=None,
    help="Only print transactions that start from the injector with opcode. [ReadShared, WriteEvictOrEvict, ...]",
)
parser.add_argument("--output", type=str, default="latency_flow.log")
args = parser.parse_args(evp_args)

LatencyFlow(args.output, args.start_transaction)
