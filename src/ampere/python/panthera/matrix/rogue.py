# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Define some components used in the Rogue product.
"""

from .components import *
from .domino import CCM2, MCU


class Blank(Tile):
    DEVICES = [None, None, None, None]


class HNF2(CpxComplex):
    """A 2-HNF complex."""

    DEVICES = [HNF] * 2


class RNF_HNF(Tile):
    DEVICES = [HNF2, None, CCM2, None]


class MCUOnly(Tile):
    DEVICES = [MCU, None, None, None]


class RDI(ADI):
    DEVICE_TYPE = "RDI"


class IDI2(Tile):
    DEVICES = [RDI, None, RDI, None]


class IDI1(Tile):
    DEVICES = [RDI, None, None, None]


class CCL0(Die):
    TILES = [
        [MCUOnly, MCUOnly, Blank, Blank, Blank, Blank],
        [Blank, RNF_HNF, RNF_HNF, RNF_HNF, RNF_HNF, MCUOnly],
        [Blank, RNF_HNF, RNF_HNF, RNF_HNF, RNF_HNF, IDI2],
        [Blank, RNF_HNF, RNF_HNF, RNF_HNF, RNF_HNF, IDI1],
        [IDI2, RNF_HNF, RNF_HNF, RNF_HNF, RNF_HNF, IDI1],
    ]


class CCL1(Die):
    TILES = [
        [Blank, Blank, Blank, Blank, MCUOnly, MCUOnly],
        [MCUOnly, RNF_HNF, RNF_HNF, RNF_HNF, RNF_HNF, Blank],
        [IDI2, RNF_HNF, RNF_HNF, RNF_HNF, RNF_HNF, Blank],
        [IDI1, RNF_HNF, RNF_HNF, RNF_HNF, RNF_HNF, Blank],
        [IDI1, RNF_HNF, RNF_HNF, RNF_HNF, RNF_HNF, IDI2],
    ]


class Rogue(SoC):
    DIES = [
        {"die_type": CCL0, "x_off": 0, "y_off": 0, "flipx": True, "rotate": -90},
        {"die_type": CCL1, "x_off": 5, "y_off": 0, "flipx": True, "rotate": 90},
        {"die_type": CCL0, "x_off": 5, "y_off": 6, "flipx": True, "rotate": 90},
        {"die_type": CCL1, "x_off": 0, "y_off": 6, "flipx": True, "rotate": -90},
    ]
