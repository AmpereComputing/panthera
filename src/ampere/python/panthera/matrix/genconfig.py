# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Experiment components -> config file generator.

Lots of required config is missing at the moment.
"""

import re
import sys
from typing import IO, List, Tuple

from .components import Component, CpxComplex, Die, Tile, devices

DEFAULT_DEVMAP = {
    "Core": "MeshInjector",
    "PxHA": "PxHA",
    "PmHA": "PmHA",
    "MCU": "Memory",
    "IOR": "IORInjector",
    "ADI": None,
    "RDI": None,
}


def gen_die_config(die: Die, prefix: str = "", devmap: dict = {}, out: IO = sys.stdout):
    reprefix = re.escape(prefix)
    dmap = DEFAULT_DEVMAP.copy()
    dmap.update(devmap)

    print(rf"""
{prefix}mesh.size_x {die.cols}
{prefix}mesh.size_y {die.rows}
{prefix}mesh.devices {max_devices(all_tiles(die))}

# Two cycles of inter-router latency and 6 router ingress credits everywhere.
# FIXME: make this configurable in panthera.matrix.genconfig

~{reprefix}mesh\.X[0-9]+_Y[0-9]+_to_X[0-9]+_Y[0-9]+.*\.latency 2

~{reprefix}mesh\.rtr_X[0-9]+_Y[0-9]+\.req_[snew]\.size 6
~{reprefix}mesh\.rtr_X[0-9]+_Y[0-9]+\.snp_[snew]\.size 6
~{reprefix}mesh\.rtr_X[0-9]+_Y[0-9]+\.rsp[01]_[snew]\.size 6
~{reprefix}mesh\.rtr_X[0-9]+_Y[0-9]+\.dat[01]_[snew]\.size 6

~{reprefix}mesh\.rtr_X[0-9]+_Y[0-9]+\.req_p[0-9]+\.size 6
~{reprefix}mesh\.rtr_X[0-9]+_Y[0-9]+\.snp_p[0-9]+\.size 6
~{reprefix}mesh\.rtr_X[0-9]+_Y[0-9]+\.rsp[01]_p[0-9]+\.size 6
~{reprefix}mesh\.rtr_X[0-9]+_Y[0-9]+\.dat[01]_p[0-9]+\.size 6

# Device list
#
{prefix}devices [
    {device_list_json(device_list(die, dmap))}
]
""")


def all_tiles(die: Die):
    return [die.tiles[x][y] for x in range(die.cols) for y in range(die.rows)]


def max_devices(tiles: List[Tile]):
    max_devices = 0
    for tile in tiles:
        max_devices = max(max_devices, len(tile.devices))
    return max_devices


def device_list(die: Die, dmap: dict):
    def include_device(dev: Component):
        return dev.port is not None and dev.dtype in dmap and dmap[dev.dtype] is not None

    devlist = []

    for d in devices(die, predicate=include_device):
        devlist.append((dmap[d.dtype], d.x, d.y, d.port, d.didx if d.didx is not None else 0))

    devlist.sort(key=lambda x: (x[0], x[2], x[1], x[3], x[4]))  # sort Y-first
    return devlist


def device_list_json(devlist: List[Tuple]):
    ds = []
    agent_id = 0

    for d in devlist:
        ds.append(f'{{ "type": "{d[0]}", "position": [{d[1]}, {d[2]}, {d[3]}, {d[4]}], "agent_id": {agent_id} }}')
        agent_id += 1

    return ",\n    ".join(ds)
