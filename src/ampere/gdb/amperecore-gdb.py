#!/usr/bin/env python
# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import gdb


class AddrPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        addr = self.val["contents"]
        return hex(int(addr))


class NodeIdPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        if str(self.val["m_nodeid_type"]) == "ampere::mesh::chi::NodeId::NodeId_Type::Local":
            x = int(self.val["m_node_id"]["local_id"]["x"])
            y = int(self.val["m_node_id"]["local_id"]["y"])
            p = int(self.val["m_node_id"]["local_id"]["port"])
            d = int(self.val["m_node_id"]["local_id"]["device"])
            return f"[{x},{y},{p},{d}]"
        else:
            soc = int(self.val["m_node_id"]["global_id"]["socket_id"])
            die = int(self.val["m_node_id"]["global_id"]["die_id"])
            agent = int(self.val["m_node_id"]["global_id"]["agent_id"])
            return f"{soc}.{die}.{agent}"


def lookup_type(val):
    stype = str(val.type)

    if stype in (
        "ampere::archx::VirtAddr",
        "ampere::archx::PhysAddr",
        "ampere::archx::ProgramCounter",
        "ampere::archx::Bitfield",
    ):
        return AddrPrinter(val)
    elif stype == "ampere::mesh::chi::NodeId":
        return NodeIdPrinter(val)
    return None


gdb.pretty_printers.append(lookup_type)
