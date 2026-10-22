# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
A Pythonic representation of components in an SoC.
"""

from collections import defaultdict
from typing import Union

import panthera.lynx as lynx


class Component(lynx.Component):
    """Base representation of some component of the die: router, core, MCU, etc."""

    DTYPE_TO_COMPONENT_TYPE = {
        "CpxComplex": lynx.ComponentType.Generic,
        "Router": lynx.ComponentType.RTR,
        "Core": lynx.ComponentType.Core,
        "PxHA": lynx.ComponentType.PxHA,
        "PmHA": lynx.ComponentType.PmHA,
        "MCU": lynx.ComponentType.MCU,
        "ADI": lynx.ComponentType.ADI,
        "IOR": lynx.ComponentType.IOR,
        "IOH": lynx.ComponentType.IOH,
        "NHP": lynx.ComponentType.NHP,
        "MHA_SYS": lynx.ComponentType.MHA_SYS,
        "MHA_GIC": lynx.ComponentType.MHA_GIC,
        "MHA_DVM": lynx.ComponentType.MHA_DVM,
        "CCL_ACR_SYS": lynx.ComponentType.CCL_ACR_SYS,
        "CCL_ACR_GIC": lynx.ComponentType.CCL_ACR_GIC,
        "CCL_ACR_CTRACE": lynx.ComponentType.CCL_ACR_CTRACE,
        "CCL_ACR_MTRACE": lynx.ComponentType.CCL_ACR_MTRACE,
        "CCL_ACR_DDR": lynx.ComponentType.CCL_ACR_DDR,
        "IOD_ACR_SYS": lynx.ComponentType.IOD_ACR_SYS,
        "IOD_ACR_GIC": lynx.ComponentType.IOD_ACR_GIC,
        "IOD_ACR_MTRACE": lynx.ComponentType.IOD_ACR_MTRACE,
        "IOD_ACR_SMMU": lynx.ComponentType.IOD_ACR_SMMU,
        "CXL": lynx.ComponentType.CXL,
        "COM": lynx.ComponentType.COM,
        "IORr": lynx.ComponentType.IORr,
        "IORw": lynx.ComponentType.IORw,
        "IOHr": lynx.ComponentType.IOHr,
        "IOHw": lynx.ComponentType.IOHw,
    }

    def __init__(self, die_id, x, y, port=None, didx=None, dtype=None):
        if port is None:
            port = -1
        if didx is None:
            didx = -1
        if dtype is None:
            dtype = self.DEVICE_TYPE
        self.dtype = dtype
        comp_type = Component.DTYPE_TO_COMPONENT_TYPE[dtype]
        super().__init__(comp_type, lynx.Location(die_id, x, y, port, didx))

    def __repr__(self):
        s = f"{self.die_id}: {self.dtype} x{self.x}y{self.y}"
        if self.port != -1:
            s += f"p{self.port}"
        if self.didx != -1:
            s += f"d{self.didx}"
        return s

    def loc(self):
        return (self.die_id, self.x, self.y)


class Router(Component):
    """A mesh router with slightly different construction than a base Component."""

    def __init__(self, die_id, x, y):
        super().__init__(die_id, x, y, None, None, "Router")


class ADI(Component):
    """ADI (Ampere Die Interconnect) is a component that points to another ADI"""

    DEVICE_TYPE = "ADI"

    def __init__(self, die_id, x, y, port):
        super().__init__(die_id, x, y, port, None, "ADI")
        self._other = None

    @property
    def other(self):
        return self._other

    @other.setter
    def other(self, val):
        if self._other is not None:
            raise RuntimeError(f"{self} is already connected to {self._other}!")
        self._other = val


class CpxComplex(Component):
    def __init__(self, die_id, x, y, port, devices=None):
        super().__init__(die_id, x, y, port, None, "CpxComplex")

        if devices is None:
            devices = self.DEVICES

        self.devices = [dev(die_id, x, y, port, didx) for didx, dev in enumerate(devices)]
        self.n = len(self.devices)


class Core(Component):
    DEVICE_TYPE = "Core"
    FILL = "#d2d200"


class HNF(Component):
    """A Home Agent."""

    DEVICE_TYPE = "HNF"
    FILL = "#92d050"


class PxHA(Component):
    """A Proxy Home Agent."""

    DEVICE_TYPE = "PxHA"
    FILL = "#92d050"


class PmHA(Component):
    """A Primary Home Agent."""

    DEVICE_TYPE = "PmHA"
    FILL = "#69a02c"


class IOR(Component):
    """An IO Requester."""

    DEVICE_TYPE = "IOR"
    FILL = "#ffff66"


class IORr(Component):
    """An IO Requester (read)."""

    DEVICE_TYPE = "IORr"
    FILL = "#ffff66"


class IORw(Component):
    """An IO Requester (write)."""

    DEVICE_TYPE = "IORw"
    FILL = "#ffff66"


class IOH(Component):
    """An IO Home Agent (Completer)."""

    DEVICE_TYPE = "IOH"
    FILL = "#92d050"


class IOHr(Component):
    """An IO Home Agent (Completer - read)."""

    DEVICE_TYPE = "IOHr"
    FILL = "#92d050"


class IOHw(Component):
    """An IO Home Agent (Completer - write)."""

    DEVICE_TYPE = "IOHw"
    FILL = "#92d050"


class MemSubChan(Component):
    """A Memory Sub-channel"""

    DEVICE_TYPE = "MCU"
    FILL = "#ab9ac0"


class CCL_ACR_SYS(Component):
    DEVICE_TYPE = "CCL_ACR_SYS"
    FILL = "#ffff66"


class CCL_ACR_GIC(Component):
    DEVICE_TYPE = "CCL_ACR_GIC"
    FILL = "#ffff66"


class CCL_ACR_CTRACE(Component):
    DEVICE_TYPE = "CCL_ACR_CTRACE"
    FILL = "#ffff66"


class CCL_ACR_MTRACE(Component):
    DEVICE_TYPE = "CCL_ACR_MTRACE"
    FILL = "#ffff66"


class CCL_ACR_DDR(Component):
    DEVICE_TYPE = "CCL_ACR_DDR"
    FILL = "#ffff66"


class IOD_ACR_SYS(Component):
    DEVICE_TYPE = "IOD_ACR_SYS"
    FILL = "#ffff66"


class IOD_ACR_GIC(Component):
    DEVICE_TYPE = "IOD_ACR_GIC"
    FILL = "#ffff66"


class IOD_ACR_MTRACE(Component):
    DEVICE_TYPE = "IOD_ACR_MTRACE"
    FILL = "#ffff66"


class IOD_ACR_SMMU(Component):
    DEVICE_TYPE = "IOD_ACR_SMMU"
    FILL = "#ffff66"


class NHP(Component):
    DEVICE_TYPE = "NHP"
    FILL = "#00cca8"


class MHA_SYS(Component):
    DEVICE_TYPE = "MHA_SYS"
    FILL = "#92d050"


class MHA_GIC(Component):
    DEVICE_TYPE = "MHA_GIC"
    FILL = "#92d050"


class MHA_DVM(Component):
    DEVICE_TYPE = "MHA_DVM"
    FILL = "#92d050"


class COM(Component):
    DEVICE_TYPE = "COM"
    FILL = "#ab9ac0"


class CXL(Component):
    DEVICE_TYPE = "CXL"
    FILL = "#66ffff"


class Tile:
    """A Tile is a container for a router and some number (up to 4, usually)
    attached devices.
    """

    DEVICES = []

    def __init__(self, die_id, x, y, devices=None):
        if devices is None:
            devices = self.DEVICES

        self.die_id = die_id
        self.x = x
        self.y = y
        self.rtr = Router(die_id, x, y)
        self.devices = []
        self.device_tags = []
        for port, dev in enumerate(devices):
            if dev is not None:
                if type(dev) is tuple:
                    dev, tag = dev
                else:
                    tag = None
                self.devices.append(dev(die_id, x, y, port))
                self.device_tags.append(tag)
            else:
                self.devices.append(None)
                self.device_tags.append(None)


class Die:
    """A die is a grid of tiles.

    Tiles are either passed in or set in a derived's class variable TILES. This
    is expected to be a two dimensional array, arranged like this:

        [
            [ Tile_x0y2, Tile_x1y2, Tile_x2y2 ],
            [ Tile_x0y1, Tile_x1y1, Tile_x2y1 ],
            [ Tile_x0y0, Tile_x1y0, Tile_x2y0 ]
        ]

    This way, the tile layout matches what you would typically see in a Visio diagram.
    The tile entries themselves may either be:

        (a) the names of Tile sub-classes; they will be instantiated with (die_id, x, y)
        (b) another list of devices; this class will create a Tile containing those devices
    """

    TILES = []
    DISCONNECTS = []

    def __init__(self, die_id=0, tiles=None, x_off=0, y_off=0, flipx=False, flipy=False, rotate=0):
        if tiles is None:
            tiles = self.TILES

        self.die_id = die_id
        self.rows = len(tiles)
        self.cols = len(tiles[0])
        self.tiles = [[[] for _ in range(self.rows)] for _ in range(self.cols)]
        self.x_off = x_off
        self.y_off = y_off
        self.flipx = flipx
        self.flipy = flipy
        self.rotate = rotate
        self.rtr_by_xy = {}
        self.dev_by_xypd = {}
        self.dev_by_type = defaultdict(lambda: [])
        self.dev_by_tag = {}
        self.disconnects = self.DISCONNECTS

        for row in range(self.rows):
            # The user writes the list from top to bottom so that it visually matches the
            # diagrams, but we store the tiles under [x][y] which feels more natural when
            # writing Python. Notice that "tiles" is indexed with row/col, and "self.tiles"
            # is indexed with x/y.
            y = self.rows - row - 1

            for col in range(self.cols):
                x = col
                if isinstance(tiles[row][col], list):
                    tile = Tile(die_id, x, y, devices=tiles[row][col])
                else:
                    tile = tiles[row][col](die_id, x, y)

                self.tiles[x][y] = tile
                self.rtr_by_xy[(self.die_id, x, y)] = tile.rtr

                for port, dev in enumerate(tile.devices):
                    if dev is None:
                        continue

                    if hasattr(dev, "devices"):
                        for didx, subdev in enumerate(dev.devices):
                            if subdev is None:
                                continue

                            self.dev_by_xypd[(die_id, x, y, port, didx)] = subdev
                            self.dev_by_type[subdev.dtype].append(subdev)
                    else:
                        self.dev_by_xypd[(die_id, x, y, port, 0)] = dev
                        self.dev_by_type[dev.dtype].append(dev)

                        if tile.device_tags[port] is not None:
                            self.dev_by_tag[(die_id, tile.device_tags[port])] = dev

    def __getitem__(self, x):
        return self.tiles[x]

    def devices(self):
        return sum(self.dev_by_type.values(), start=[])

    def to_lynx(self, model, omit_colocated_devices=()):
        for col in self.tiles:
            for tile in col:
                model.add_component(tile.rtr)
                for dev in tile.devices:
                    if hasattr(dev, "devices"):
                        seen_type = set()
                        for subdev in dev.devices:
                            if subdev is None:
                                continue
                            if subdev.type in omit_colocated_devices and subdev.type in seen_type:
                                continue
                            model.add_component(subdev)
                            seen_type.add(subdev.type)
                    elif dev is not None:
                        model.add_component(dev)


class SoC:
    """An SoC is an arrangement of Dies.

    Some of the presentation aspects of an SoC (how the dies are oriented, for example),
    have bled over into the construction of this object. It just seemed easier to
    define the die and their orientations together in one place.

    Thus a DIES list might look something like this:

        DIES = [
            {'die_type': CCL,                            'rotate': -90},
            {'die_type': CCL, 'y_off': 4, 'flipy': true, 'rotate':  90},
            {'die_type': IOD, 'x_off': 4}
        ]

    In this example, `CCL` and `IOD` are presumably sub-classes of `Die`. The 'flipy'
    and 'rotate' fields are transformations applied to the SVG render (in that order),
    and 'x_off' and 'y_off' are offsets counted in Tiles when drawing. This example
    might render the SoC like this:

        +---+---+---+---+
        |3,3|   |   |   |
        +---+---+---+---+
        |   | CCL 0 |   |
        +---+---+---+---+ +---+---+---+---+
        |   |   |   |   | |   |   |   |   |
        +---+---+---+---+ +---+---+---+---+
        |   |   |   |0,0| |   | IOD 2 |   |
        +===+===+===+===+ +---+---+---+---+
        |   |   |   |0,0| |   |   |   |   |
        +---+---+---+---+ +---+---+---+---+
        |   | CCL 1 |   | |   |   |   |   |
        +---+---+---+---+ +---+---+---+---+
        |   |   |   |   |
        +---+---+---+---+
        |3,3|   |   |   |
        +---+---+---+---+

    """

    DIES = []

    def __init__(self, dies=None, connections=None):
        if dies is None:
            dies = self.DIES

        self.dies = []
        self.rtr_by_xy = {}
        self.dev_by_xypd = {}
        self.dev_by_type = defaultdict(lambda: [])
        self.dev_by_tag = {}
        self.rdi_route = defaultdict(lambda: [])

        for i, die_info in enumerate(dies):
            die_id = die_info.get("die_id", i)
            x_off = die_info.get("x_off", 0)
            y_off = die_info.get("y_off", 0)
            flipx = die_info.get("flipx", False)
            flipy = die_info.get("flipy", False)
            rotate = die_info.get("rotate", 0)
            die = die_info["die_type"](die_id, x_off=x_off, y_off=y_off, flipx=flipx, flipy=flipy, rotate=rotate)
            self.dies.append(die)
            self.rtr_by_xy.update(die.rtr_by_xy)
            self.dev_by_xypd.update(die.dev_by_xypd)
            self.dev_by_tag.update(die.dev_by_tag)
            for type, devlist in die.dev_by_type.items():
                self.dev_by_type[type] += devlist

        if connections is None:
            connections = getattr(self, "CONNECTIONS", None)

        if connections:
            for from_dev, to_dev in connections:
                try:
                    self.connect_rdi(self.dev_by_tag[from_dev], self.dev_by_tag[to_dev])
                except KeyError:
                    pass

    def devices(self):
        return sum(self.dev_by_type.values(), start=[])

    def connect_rdi(self, rdi1, rdi2):
        assert rdi1.die_id != rdi2.die_id

        rdi1.other = rdi2
        rdi2.other = rdi1

        self.rdi_route[(rdi1.die_id, rdi2.die_id)].append([rdi1])
        self.rdi_route[(rdi2.die_id, rdi1.die_id)].append([rdi2])

    def to_lynx(self, model, omit_colocated_devices=()):
        for die in self.dies:
            die.to_lynx(model, omit_colocated_devices)

        for adi in self.dev_by_type["ADI"]:
            if adi.other is not None:
                model.connect_components_one_way(adi, adi.other)


def tiles(die: Die):
    for x in range(die.cols):
        for y in range(die.rows):
            yield die.tiles[x][y]


def devices(c: Union[Die, SoC], predicate=None):
    if isinstance(c, SoC):
        for die in c.dies:
            yield from devices(die)
    else:
        for tile in tiles(c):
            for device in tile.devices:
                if device is None:
                    continue
                if predicate is None or predicate(device):
                    yield device
                if hasattr(device, "devices"):
                    for subdev in device.devices:
                        if predicate is None or predicate(subdev):
                            yield subdev
