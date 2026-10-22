# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import evp


class TransactionTracker:
    """
    TransactionTracker watches transactions, recording parent/child
    relationships between them, and allowing the user to record data items on
    individual transactions when they occur and query the transaction tree
    for recorded data.

    A user (typically a subclass) of TransactionTracker can record data
    against a given transaction using add_tx_data(), and query that data using
    get_tx_data(), optionally also searching that transaction's direct
    ancestors with search_parents=True.

    TransactionTracker holds onto any parent transaction and its data as long
    as that transaction has any active children. This allows child transactions
    to query data items on their parents/ancestors even if those ancestors
    transactions have already ended.

    A usage example can be found in the branch_mispredictions.py plugin
    """

    def __init__(self, on_start_tx=lambda txid: None, on_end_tx=lambda txid: None):
        self.parents = {}  # maps child txid to immediate parent txid
        self.children = {}  # maps parent txid to immediate children txid's
        self.tx_data = {}  # maps txid to recorded data for that transaction

        # callback functions for start/end transactions
        self.start_tx_callback = on_start_tx
        self.end_tx_callback = on_end_tx

        evp.on("start_transaction", lambda e: self._start_tx(e))
        # RTL event streams do not currently have end_transaction events. This
        # is on a TODO list, but in the meantime fall back to processing all
        # transaction-ending behavior at the end of simulation if the
        # end_transaction event is not defined.
        if evp.has_definition("end_transaction"):
            evp.on("end_transaction", lambda e: self._end_tx(e))
        evp.end_simulation(lambda: self._end_transactions_at_end())

    def _start_tx(self, event):
        txid = event.data["txid"]
        self.tx_data[txid] = {}
        if "parent" in event.data:
            parent = event.data["parent"]
            self.parents[txid] = parent
            if parent not in self.children:
                self.children[parent] = set()
            self.children[parent].add(txid)

        self.start_tx_callback(txid)

    def add_tx_data(self, txid, key, value, ancestor_generations=0):
        """
        Add the data item to the tracked transaction, and optionally up to
        `ancestor_generations` levels of parents, if they exist
        """
        for generation in range(0, ancestor_generations + 1):
            if txid in self.tx_data:
                self.tx_data[txid][key] = value

            # Recurse if parent exists, exit otherwise
            if txid in self.parents:
                txid = self.parents[txid]
            else:
                return

    def get_tx_data(self, txid, key, search_parents=False):
        if txid not in self.tx_data:
            return None

        tx_data = self.tx_data[txid]
        if key in tx_data:
            return tx_data[key]
        elif search_parents and txid in self.parents:
            return self.get_tx_data(self.parents[txid], key, True)
        else:
            return None

    def _remove_ghost_transactions(self, txid):
        # Do not remove this transaction if it still has children which are
        # still tracked. Effectively, ensure that a transaction's data in
        # self.tx_data outlives all of its children. Then, whenever a
        # transaction ends (and it has no remaining living children),
        # recursively remove any parents which also no longer have children.

        # First, make sure the current transaction has ended (i.e. "__ghost__"
        # is set). If it hasn't ended, we can't remove it or any of its
        # parents.
        if not self.get_tx_data(txid, "__ghost__"):
            return

        # Next, make sure that this transaction does not have any active
        # children. If it does, we can't remove it or any of its parents.
        if txid in self.children:
            return

        # If this transaction has a parent, remove ourselves as one of its
        # active children (removing the set of children if we are the last
        # active child).
        if txid in self.parents:
            parent = self.parents[txid]
            del self.parents[txid]
            self.children[parent].remove(txid)
            if len(self.children[parent]) == 0:
                del self.children[parent]

            # recurse, removing any parents which are ghosts and have no living
            # children
            self._remove_ghost_transactions(parent)

        # finally, remove this transaction's data since we are done with it
        del self.tx_data[txid]

    def _end_tx(self, event):
        txid = event.data["txid"]
        if txid not in self.tx_data:
            return

        self.end_tx_callback(txid)

        self.add_tx_data(txid, "__ghost__", True)
        self._remove_ghost_transactions(txid)

    def _end_transactions_at_end(self):
        for txid in self.tx_data.keys():
            self.end_tx_callback(txid)
