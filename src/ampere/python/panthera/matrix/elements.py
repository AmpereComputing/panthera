# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import math
from collections import defaultdict

"""
Define various SVG elements for drawing Matrix components.

These include:

   Routers (RTR)
   Cores   (CCM)
   PHAs    (PxHA and PmHA)
   RDI     (RDI)

as well as the larger containers "Tile" and "Die".

Each element provides a `render` method for returning an SVG string that
can be rendered by a web browser or in a Jupyter Notebook.

For a Jupyter Notebook, you'll need code like this:

    from IPython.display import SVG   # or, alternatively, HTML

    SVG(my_component.render())

The `render` method can be given keyword arguments that are interpolated
into the `svg` element attributes (e.g., `x`, `y`, `viewBox`, etc.).

For construction, the individual elements have their own parameter
definitions. See the class descriptions below for details.
"""


def port_is_upper(port):
    """Ports 1 and 2 are above the RTR, 0 and 3 are below.

    Like this:

        1 | 2
       ---+---
        0 | 3

    We need to know which hemisphere we're in so we can flip some elements
    (like the position of a CPX) appropriately.
    """
    return port in {1, 2}


class Element:
    """Base class for all elements.

    The `svg` method wraps an arbitrary string with an `svg` XML element (thus
    all elements are rendered as nested SVGs). `svg` can take keyword arguments
    which are interpolated into the attributes of the XML string that is produced.
    """

    def svg(self, s, **attrs):
        attr_str = " ".join([f'{k}="{v}"' for k, v in attrs.items()])
        return f"<svg {attr_str}>\n  {s}\n</svg>\n"


class Component(Element):
    """Base representation of some component of the die: router, core, MCU, etc.

    A component is a device that has a location on a die. Some components, like
    routers, have an X,Y position but not a port or device index.
    """

    __slots__ = ("die_id", "x", "y", "port", "didx", "dtype")

    def __init__(self, die_id, x, y, port, didx, dtype):
        self.die_id = die_id
        self.x = x
        self.y = y
        self.port = port
        self.didx = didx
        self.dtype = dtype

    def __repr__(self):
        if self.port is None:
            return f"{self.die_id}: {self.dtype} x{self.x}y{self.y}"
        else:
            return f"{self.die_id}: {self.dtype} x{self.x}y{self.y}p{self.port}d{self.didx}"

    def loc(self):
        return (self.die_id, self.x, self.y)


class RTR(Component):
    r"""RTR is rendered as an octagon where each side is 50 units long:

        ..50..
        +----+...35
       /     .\
      /      . \
     +       .  + .
     |      35  | . 50
     |          | .
     +          +
      \        /
       \      /
        +----+

    This means the slanted sides are the hypoteneuse of a right triangle
    with its other sides ~35 units (50/sqrt(2)).

    We start drawing at the upper left vertex.

    Construction:

        RTR(x, y, **attrs)

        Required:
            x                X coordinate of the router on this die
            y                Y coordinate of the router on this die

        Optional:
            fill=<color>     Fill color
            stroke=<color>   Stroke color
    """

    DEVICE_TYPE = "Router"
    SIDE = 50
    WIDTH = 50 + 35 * 2
    HEIGHT = 50 + 35 * 2
    FILL = "#dbeef3"
    STROKE = "black"

    def __init__(self, die_id, x, y, **attrs):
        super().__init__(die_id, x, y, None, None, self.DEVICE_TYPE)
        self.xy_overrides = set()
        self.path = f"x{x}y{y}"
        self.fill = attrs.pop("fill", self.FILL)
        self.stroke = attrs.pop("stroke", self.STROKE)

    def add_xy_override(self, tgt):
        assert self.die_id == tgt.die_id
        self.xy_overrides.add((tgt.x, tgt.y, tgt.port))

    def route_y_first(self, tgt):
        return (tgt.x, tgt.y, tgt.port) in self.xy_overrides

    def render(self, **attrs):
        center_x = self.WIDTH / 2
        center_y = self.HEIGHT / 2
        id = f"x{self.x}y{self.y}"
        return self.svg(
            f'<path id="{id}" d="M 35 0 h 50 l 35 35 v 50 l -35 35 h -50 l -35 -35 v -50 Z" fill="{self.fill}" stroke="{self.stroke}"/><text x="{center_x}" y="{center_y}">{self.x},{self.y}</text>',
            **attrs,
        )


class CpxComplex(Component):
    """CpxComplex is a base class for devices behind a CPX.

    It is built with a parameter indicating how many devices there are,
    and drawn as that many rectangles with CPX box on the bottom (or top,
    depending on whether they are positioned above or below the router):

        +---+---+
        |   |   |
        |   |   |  <-- 2 devices
        |   |   |
        +---+---+
        |       |  <-- CPX (if above the RTR)
        +-------+

    Construction:

        CpxComplex(x, y, port, n, **attrs)

        Required:
            x                X coordinate of the router this complex is attached to
            y                Y coordinate of the router this complex is attached to
            port             Which device port (0, 1, 2, 3) this complex is attached to
            n                How many devices to render

        Optional:
            fill=<color>     Fill color (devices)
            stroke=<color>   Stroke color (devices)
    """

    WIDTH = 35
    HEIGHT = 75
    STROKE = "black"
    FILL = "#d2d200"
    CPX_HEIGHT = 25
    CPX_FILL = "#dbeef3"

    def __init__(self, die_id, x, y, port, n, **attrs):
        super().__init__(die_id, x, y, port, 0, self.DEVICE_TYPE)
        self.n = n
        self.path = f"x{x}y{y}.port{port}"
        self.width = n * self.WIDTH
        self.height = self.HEIGHT + self.CPX_HEIGHT
        self.fill = attrs.pop("fill", self.FILL)
        self.stroke = attrs.pop("stroke", self.STROKE)
        self.dev_attrs = [attrs.get(f"dev{i}", {}) for i in range(self.n)]

    def render(self, **attrs):
        s = ""
        port = attrs.get("port", self.port)
        y = 0 if port_is_upper(port) else self.CPX_HEIGHT

        for i in range(self.n):
            dev_attrs = self.dev_attrs[i]
            stroke = self.stroke
            fill = self.fill[i] if isinstance(self.fill, list) else self.fill
            if "color" in self.dev_attrs:
                stroke = dev_attrs["color"]
                fill = dev_attrs["color"]
            stroke = dev_attrs.get("stroke", stroke)
            fill = dev_attrs.get("fill", fill)

            s += f'  <rect width="{self.WIDTH}" x="{self.WIDTH * i}" y="{y}" height="{self.HEIGHT}" stroke="{stroke}" fill="{fill}"/>\n'

        for i in range(self.n):
            dev_attrs = self.dev_attrs[i]

            if "label" in dev_attrs:
                label = dev_attrs["label"]

                center_x = (
                    (self.WIDTH * i) + (self.WIDTH // 2) - (len(label) // 2) * 8
                )  # not sure how to adjust this text
                center_y = y + self.HEIGHT // 2
                s += f'<text x="{center_x}" y="{center_y}" stroke="black">{label}</text>\n'

        y = self.HEIGHT if port_is_upper(port) else 0
        s += f'  <rect y="{y}" width="{self.width}" height="{self.CPX_HEIGHT}" stroke="{self.STROKE}" fill="{self.CPX_FILL}"/>\n'

        return self.svg(s, **attrs)


class CCM(CpxComplex):
    """CCM is a set of cores behind a CPX.

    See CpxComplex help for details.
    """

    DEVICE_TYPE = "Core"


class HNF(CpxComplex):
    """A Home Agent complex.

    See CpxComplex help for details.
    """

    DEVICE_TYPE = "HNF"
    FILL = "#92d050"


class PxHA(CpxComplex):
    """A Proxy Home Agent complex.

    See CpxComplex help for details.
    """

    DEVICE_TYPE = "PxHA"
    FILL = "#92d050"


class PmHA(CpxComplex):
    """A Primary Home Agent complex.

    See CpxComplex help for details.
    """

    DEVICE_TYPE = "PmHA"
    FILL = "#69a02c"


class RDI(Component):
    """A simple rectangle representing an RDI link.

    Construction:

        RDI(die_id, x, y, **attrs)

        Required:
            x                X coordinate of the router this RDI is attached to
            y                Y coordinate of the router this RDI is attached to
            port             Which device port (0, 1, 2, 3) this RDI is attached to

        Optional:
            fill=<color>     Fill color
            stroke=<color>   Stroke color
    """

    DEVICE_TYPE = "RDI"
    WIDTH = 100
    HEIGHT = 70
    STROKE = "black"
    FILL = "#923931"

    def __init__(self, die_id, x, y, port, **attrs):
        super().__init__(die_id, x, y, port, 0, self.DEVICE_TYPE)
        self._other = None
        self.path = f"x{x}y{y}.port{port}"
        self.fill = attrs.pop("fill", self.FILL)
        self.stroke = attrs.pop("stroke", self.STROKE)
        self.width = self.WIDTH
        self.height = self.HEIGHT

    @property
    def other(self):
        return self._other

    @other.setter
    def other(self, val):
        if self._other is not None:
            raise RuntimeError(f"{self} is already connected to {self._other}!")
        self._other = val

    def render(self, **attrs):
        return self.svg(
            f'<rect width="{self.WIDTH}" height="{self.HEIGHT}" fill="{self.fill}" stroke="{self.stroke}"/>', **attrs
        )


class ADI(RDI):
    DEVICE_TYPE = "ADI"


class IOR(CpxComplex):
    DEVICE_TYPE = ["AcXL2", "ACB", "IOR"]
    FILL = ["#66ffff", "#66ffff", "#ffff66"]

    def __init__(self, die_id, x, y, port, **attrs):
        super().__init__(die_id, x, y, port, 3, **attrs)


class MCU(CpxComplex):
    """A Memory Control Unit.

    See CpxComplex help for details."""

    DEVICE_TYPE = "MCU"
    FILL = "#ab9ac0"

    def __init__(self, die_id, x, y, port, **attrs):
        super().__init__(die_id, x, y, port, 2, **attrs)


class Tile(Element):
    """A Tile holds a router and optional devices around it.

    The Tile is drawn in a 400x400 box with a RTR in the middle. If a device list is given,
    it is interpreted as a device per port where the port is the list index. So for example:

        Tile(0, 0, devices=[None, DevOnPort1, DevOnPort2, None])

    would produce a Tile at X=0, Y=0 with two devices attached; one at port 1 and one at
    port 2 (the upper two ports), while ports 0 and 3 are empty. The devices are rendered
    in the quadrants around the router:

         1 | 2
        ---+---
         0 | 3

    Each device is really a callable (so perhaps a class name) which must be constructible
    like this:

        Dev(x, y, port, **attrs)

    Notice that NOT all of the elements above actually satisfy this constraint. A CCM, for
    example, needs to be told how many cores to instantiate. Thus it will need some kind of
    a wrapper to be usable in a Tile. This could be a simple function, or it could be a
    child class. The latter case would look something like this:

        class CCM4(CCM):
            def __init__(self, x, y, port, **attrs):
                super().__init__(x, y, port, 4, **attrs)

    Then you could use it:

        Tile(0, 0, devices=[CCM4, CCM4, None, None])

    If a `devices` keyword argument is not supplied, a Tile will look at its own `DEVICES`
    attribute (a class attribute). This allows for the declaration of re-usable child
    classes:

        CCM4_PxHA2(Tile):
            DEVICES = [CCM4, None, PxHA2, None]

    And now `CCM4_PxHA2` can be easily stamped out many times in a `Die`.

    Architecture specific elements like `CCM4` and `CCM4_PxHA2` are not defined in this
    module, but can be found in neighboring product-specific modules.

    Keyword arguments form a dictionary which is then supplied piece-wise to the underlying
    elements. Each element has a "path" which depends on its geographic location. Any sub-
    dictionary that is keyed by a given path is supplied to the matching element.

    For example, the router at the location 2,3 is simply called "x2y3", so attributes passed
    under that name will apply to the router itself:

        Tile(2, 3, devices=None, x2y3={'fill': 'red'})
            ==> make the RTR at 2,3 red instead of its default

    Devices have an additional port in their name:

        Tile(0, 1, devices=[CCM4], **{'x0y1.port0': {'stroke': 'blue'}})
            ==> make the CCM on port 0 have blue stroke lines
    """

    WIDTH = 400
    HEIGHT = 400

    PORT_OFFSETS = [(100, 290), (100, 10), (300, 10), (300, 290)]

    DEVICES = []

    def __init__(self, die_id, x, y, devices=None, **attrs):
        if devices is None:
            devices = self.DEVICES

        self.die_id = die_id
        self.rtr = RTR(die_id, x, y, **attrs.get(f"x{x}y{y}", {}))
        self.devices = []
        self.device_attrs = []

        if devices is not None:
            for port, dev in enumerate(devices):
                if dev is not None:
                    path = f"x{x}y{y}.port{port}"
                    device_attrs = attrs.get(path, {})
                    self.devices.append(dev(die_id, x, y, port, **device_attrs))
                    self.device_attrs.append(device_attrs)
                else:
                    self.devices.append(None)
                    self.device_attrs.append({})

    def render(self, **attrs):
        flipx = attrs.get("flipx", False)
        flipy = attrs.get("flipy", False)
        rotate = attrs.get("rotate", 0)

        center_x = self.WIDTH / 2
        center_y = self.HEIGHT / 2

        s = self.rtr.render(x=center_x - self.rtr.WIDTH / 2, y=center_y - self.rtr.HEIGHT / 2)

        for port, device in enumerate(self.devices):
            if device is not None:
                dev_link_attrs_in = self.device_attrs[port].get("to_rtr", {})
                dev_link_attrs_out = self.device_attrs[port].get("to_dev", {})

                if flipx and flipy:
                    port = {0: 2, 1: 3, 2: 0, 3: 1}[port]
                elif flipx:
                    port = {0: 3, 1: 2, 2: 1, 3: 0}[port]
                elif flipy:
                    port = {0: 1, 1: 0, 2: 3, 3: 2}[port]

                if rotate == -90:
                    port = 3 if port == 0 else port - 1
                elif rotate == 90:
                    port = 0 if port == 3 else port + 1
                elif rotate == 0:
                    pass
                else:
                    raise RuntimeError(f"rotate {rotate} is not yet implemented")

                x_port_off, y_port_off = Tile.PORT_OFFSETS[port]
                s += device.render(x=x_port_off - device.width / 2, y=y_port_off, port=port)

                inset = 8  # how far in from the octagon's vertices to draw the link arrows

                if port == 0:
                    link_x = center_x - (self.rtr.SIDE / 2) - inset
                    link_y = center_y + (self.rtr.HEIGHT / 2) - inset
                    label_in_x = link_x - 25
                    label_in_y = link_y + 25
                    label_out_x = link_x - 18
                    label_out_y = link_y - 18
                    rot = -135
                elif port == 1:
                    link_x = center_x - (self.rtr.WIDTH / 2) + inset
                    link_y = center_y - (self.rtr.SIDE / 2) - inset
                    label_in_x = link_x - 25
                    label_in_y = link_y - 25
                    label_out_x = link_x + 18
                    label_out_y = link_y - 18
                    rot = -45
                elif port == 2:
                    link_x = center_x + (self.rtr.SIDE / 2) + inset
                    link_y = center_y - (self.rtr.HEIGHT / 2) + inset
                    label_in_x = link_x + 25
                    label_in_y = link_y - 25
                    label_out_x = link_x + 18
                    label_out_y = link_y + 18
                    rot = 45
                elif port == 3:
                    link_x = center_x + (self.rtr.WIDTH / 2) - inset
                    link_y = center_y + (self.rtr.SIDE / 2) + inset
                    label_in_x = link_x + 25
                    label_in_y = link_y + 25
                    label_out_x = link_x - 18
                    label_out_y = link_y + 18
                    rot = 135

                stroke_in = "black"
                stroke_out = "black"
                fill_in = "black"
                fill_out = "black"
                if "color" in dev_link_attrs_in:
                    stroke_in = dev_link_attrs_in["color"]
                    fill_in = dev_link_attrs_in["color"]
                if "color" in dev_link_attrs_out:
                    stroke_out = dev_link_attrs_out["color"]
                    fill_out = dev_link_attrs_out["color"]
                stroke_in = dev_link_attrs_in.get("stroke", stroke_in)
                stroke_out = dev_link_attrs_out.get("stroke", stroke_out)
                fill_in = dev_link_attrs_in.get("fill", fill_in)
                fill_out = dev_link_attrs_out.get("fill", fill_out)

                # The device links are a pair of arrows, drawn like this in local coordinates:
                #
                #    |  ^
                #    |  |
                #    v  |
                #    .
                #     .... (100,100)
                #
                # The tip of the downward facing arrow is my "origin". SVG automatically clips things
                # that go negative, so I couldn't actually make this (0,0). Instead I just arbitrarily
                # added 100 to both coordinates so that this pair of arrows can be rotated without any
                # clipping. Then it will be translated into place below.
                #
                dev_links = f'<g transform="rotate({rot} 100 100)">'
                dev_links += f'<path d="M 100 100 l -5 -10 h 10 Z" fill="{fill_in}"/>'
                dev_links += f'<line x1="100" y1="95" x2="100" y2="65" stroke="{stroke_in}" stroke-width="4"/>'
                dev_links += f'<path d="M 125 65 l -5 10 h 10 Z" fill="{fill_out}"/>'
                dev_links += f'<line x1="125" y1="70" x2="125" y2="100" stroke="{stroke_out}" stroke-width="4"/>'
                dev_links += "</g>"

                s += f'<svg x="{link_x - 100}" y="{link_y - 100}">{dev_links}</svg>\n'

                if "label" in dev_link_attrs_in:
                    label = dev_link_attrs_in["label"]

                    s += "<g>\n"
                    s += f'<circle cx="{label_in_x}" cy="{label_in_y}" r="12" stroke="black" fill="darkslategray"/>\n'
                    s += f'<text x="{label_in_x - 9}" y="{label_in_y + 5}" stroke="white">{label}</text>\n'

                    if "title" in dev_link_attrs_in:
                        s += f"<title>{dev_link_attrs_in['title']}</title>\n"

                    s += "</g>\n"

                if "label" in dev_link_attrs_out:
                    label = dev_link_attrs_out["label"]

                    s += "<g>\n"
                    s += f'<circle cx="{label_out_x}" cy="{label_out_y}" r="12" stroke="black" fill="darkslategray"/>\n'
                    s += f'<text x="{label_out_x - 9}" y="{label_out_y + 5}" stroke="white">{label}</text>\n'

                    if "title" in dev_link_attrs_out:
                        s += f"<title>{dev_link_attrs_out['title']}</title>\n"

                    s += "</g>\n"

        return self.svg(s, **attrs)


class Die(Element):
    """Die is a rectangle of Tiles.

    A Die expects to be given a two-dimensional array of Tiles. Each Tile must be
    constructible like this:

        Tile(x, y, **attrs)

    Similar to a Tile, a Die can either be given a list of tiles in its constructor,
    or it will fall back on the (class) attribute `TILES`. For example:

        Die(tiles=[[CCM4_PxHA2, CCM4_PxHA2], [CCM4_PxHA2, CCM4_PxHA2]])
            ==> A 2x2 Die where each tile contains 4 cores and 2 proxies

    or equivalently:

        class TwoByTwoDie(Die):
            TILES = [
                [CCM4_PxHA2, CCM4_PxHA2],
                [CCM4_PxHA2, CCM4_PxHA2]
            ]

    The outer dimension of the tile list is Y, starting from the top. This is somewhat
    counter intuitive, but works well visually (if you arrange the TILES list like the
    above example).

    All the `attrs` are passed down to the Tiles. Since each Tile is looking for
    attributes with a given X,Y coordinate, only some will be used by each Tile.

    In addition, a Die draws connections between routers. These directional arrows are
    also addressable, and have names like:

        x0y0.north
        x0y0.south
        x0y0.east
        x0y0.west

    Thus you can alter the rendering of a particular link:

        TwoByTwoDie(**{'x0y0.north': {'color': 'red'}})
            ==> Draw the link going north from 0,0 in red

    Note here that "color" is shorthand for setting "stroke" and "fill". This is a
    shortcut that is not yet everywhere in the code.
    """

    TILES = []

    START_X = 0
    START_Y = 0

    def __init__(self, die_id, tiles=None, **attrs):
        self.die_id = die_id
        self.rtr_by_xy = {}
        self.dev_by_xypd = {}
        self.dev_by_type = defaultdict(lambda: [])
        if tiles is None:
            tiles = self.TILES
        self.rows = len(tiles)
        self.cols = len(tiles[0])
        self.tiles = [[[] for _ in range(self.cols)] for _ in range(self.rows)]
        self.width = Tile.WIDTH * self.cols
        self.height = Tile.HEIGHT * self.rows

        for y_idx in range(self.rows):
            # we build from bottom to top, but the user writes the list from
            # top to bottom so that it visually matches the diagrams; thus
            # this adjustment for y
            y = self.rows - y_idx - 1

            for x in range(self.cols):
                named_x = x + self.START_X
                named_y = y + self.START_Y
                if isinstance(tiles[y_idx][x], list):
                    new_tile = Tile(die_id, named_x, named_y, devices=tiles[y_idx][x], **attrs)
                else:
                    new_tile = tiles[y_idx][x](die_id, named_x, named_y, **attrs)
                self.tiles[y_idx][x] = new_tile

                self.rtr_by_xy[(die_id, x, y)] = new_tile.rtr

                for port, dev in enumerate(new_tile.devices):
                    if dev is not None:
                        if isinstance(dev.DEVICE_TYPE, list):
                            dtypes = dev.DEVICE_TYPE
                        else:
                            dtypes = [dev.DEVICE_TYPE]

                        for didx, dtype in enumerate(dtypes):
                            self.dev_by_xypd[(die_id, x, y, port, didx)] = dev
                            self.dev_by_type[dtype].append(dev)

        self.attrs = attrs  # keep these around for the links

    @staticmethod
    def _rtr_coord(x, y):
        rtr_x = 400 * x + 200
        rtr_y = 400 * y + 200
        return rtr_x, rtr_y

    def render(self, **attrs):
        s = ""

        flipx = attrs.get("flipx", False)
        flipy = attrs.get("flipy", False)
        rotate = int(attrs.get("rotate", 0))
        assert rotate in {0, 90, -90}

        if rotate == 90 or rotate == -90:
            s += self._draw_grid_lines(Tile.HEIGHT, Tile.WIDTH, self.cols, self.rows)
        else:
            s += self._draw_grid_lines(Tile.WIDTH, Tile.HEIGHT, self.rows, self.cols)

        s += self._draw_tiles(flipx, flipy, rotate)
        s += self._draw_rtr_connections(flipx, flipy, rotate)

        scale = attrs.pop("scale", 1.0)

        if "width" not in attrs:
            if rotate in {90, -90}:
                attrs["width"] = int(self.height * scale)
            else:
                attrs["width"] = int(self.width * scale)

        if "height" not in attrs:
            if rotate in {90, -90}:
                attrs["height"] = int(self.width * scale)
            else:
                attrs["height"] = int(self.height * scale)

        if scale is not None and "viewBox" not in attrs:
            attrs["viewBox"] = f"0 0 {int(attrs['width'] / scale)} {int(attrs['height'] / scale)}"

        return self.svg(s, **attrs)

    @staticmethod
    def _draw_grid_lines(tile_width, tile_height, rows, cols):
        s = ""
        for x in [tile_width * i for i in range(cols + 1)]:
            s += f'<line x1="{x}" y1="0" x2="{x}" y2="{tile_height * rows}" stroke="black" stroke-dasharray="4,4" stroke-dashoffset="2"/>\n'
        for y in [tile_height * i for i in range(rows + 1)]:
            s += f'<line x1="0" y1="{y}" x2="{tile_width * cols}" y2="{y}" stroke="black" stroke-dasharray="4,4" stroke-dashoffset="2"/>\n'
        return s

    def _draw_tiles(self, flipx, flipy, rotate):
        s = ""

        # Note: row 0 here means the maximum Y value for the die (not the maximum Y for the SVG!)
        # because the tiles array is written upside down from a natural Python list indexing
        # perspective.
        for row, row_tiles in enumerate(self.tiles):
            if flipy:
                row = len(self.tiles) - row - 1
            for col, tile in enumerate(row_tiles):
                if flipx:
                    col = len(row_tiles) - col - 1
                if rotate == -90:
                    y = (self.cols - col - 1) * Tile.HEIGHT
                    x = row * Tile.WIDTH
                elif rotate == 90:
                    y = col * Tile.HEIGHT
                    x = (self.rows - row - 1) * Tile.WIDTH
                elif rotate == 0:
                    x = col * Tile.WIDTH
                    y = row * Tile.HEIGHT
                else:
                    raise RuntimeError(f"rotate={rotate} is not yet implemented")
                s += tile.render(x=x, y=y, flipx=flipx, flipy=flipy, rotate=rotate)
        return s

    def _draw_rtr_connections(self, flipx, flipy, rotate):
        s = ""
        if rotate in {90, -90}:
            rtr_x_off = RTR.HEIGHT / 2
            rtr_y_off = RTR.WIDTH / 2
        else:
            rtr_x_off = RTR.WIDTH / 2
            rtr_y_off = RTR.HEIGHT / 2

        # `x` proceeds from left to right, same as how the real part is numbered
        # `y` proceeds from top to bottom, like SVG, which is opposite how the real part is numbered
        #     thus `actual_y` which factors in to the logical name of the connection
        for x in range(self.cols):
            named_x = x + self.START_X

            if flipx:
                named_x = self.cols - named_x - 1

            for y in range(self.rows):
                actual_y = self.rows - y - 1
                named_y = actual_y + self.START_Y

                if flipy:
                    named_y = self.rows - named_y - 1

                if rotate == 0:
                    if x > 0:
                        start_x, start_y = Die._rtr_coord(x, y)
                        stop_x, stop_y = Die._rtr_coord(x - 1, y)
                        start_x -= rtr_x_off
                        stop_x += rtr_x_off
                        if flipx:
                            names = (f"x{named_x}y{named_y}.east", f"x{named_x + 1}y{named_y}.west")
                        else:
                            names = (f"x{named_x}y{named_y}.west", f"x{named_x - 1}y{named_y}.east")
                        s += self._draw_rtr_lines(start_x, start_y, stop_x, stop_y, names)
                    if y > 0:
                        start_x, start_y = Die._rtr_coord(x, y)
                        stop_x, stop_y = Die._rtr_coord(x, y - 1)
                        start_y -= rtr_y_off
                        stop_y += rtr_y_off

                        if flipy:
                            names = (f"x{named_x}y{named_y}.south", f"x{named_x}y{named_y - 1}.north")
                        else:
                            names = (f"x{named_x}y{named_y}.north", f"x{named_x}y{named_y + 1}.south")
                        s += self._draw_rtr_lines(start_x, start_y, stop_x, stop_y, names)
                elif rotate == -90:
                    if x > 0:
                        start_x, start_y = Die._rtr_coord(y, self.cols - x - 1)
                        stop_x, stop_y = Die._rtr_coord(y, self.cols - x)
                        start_y += rtr_y_off
                        stop_y -= rtr_y_off
                        if flipx:
                            names = (f"x{named_x}y{named_y}.east", f"x{named_x + 1}y{named_y}.west")
                        else:
                            names = (f"x{named_x}y{named_y}.west", f"x{named_x - 1}y{named_y}.east")
                        s += self._draw_rtr_lines(start_x, start_y, stop_x, stop_y, names)
                    if y > 0:
                        start_x, start_y = Die._rtr_coord(y, self.cols - x - 1)
                        stop_x, stop_y = Die._rtr_coord(y - 1, self.cols - x - 1)
                        start_x -= rtr_x_off
                        stop_x += rtr_x_off
                        if flipy:
                            names = (f"x{named_x}y{named_y}.south", f"x{named_x}y{named_y - 1}.north")
                        else:
                            names = (f"x{named_x}y{named_y}.north", f"x{named_x}y{named_y + 1}.south")
                        s += self._draw_rtr_lines(start_x, start_y, stop_x, stop_y, names)
                elif rotate == 90:
                    if x > 0:
                        start_x, start_y = Die._rtr_coord(self.rows - y - 1, x)
                        stop_x, stop_y = Die._rtr_coord(self.rows - y - 1, x - 1)
                        start_y -= rtr_y_off
                        stop_y += rtr_y_off
                        if flipx:
                            names = (f"x{named_x}y{named_y}.east", f"x{named_x + 1}y{named_y}.west")
                        else:
                            names = (f"x{named_x}y{named_y}.west", f"x{named_x - 1}y{named_y}.east")
                        s += self._draw_rtr_lines(start_x, start_y, stop_x, stop_y, names)
                    if y > 0:
                        start_x, start_y = Die._rtr_coord(self.rows - y - 1, x)
                        stop_x, stop_y = Die._rtr_coord(self.rows - y, x)
                        start_x += rtr_x_off
                        stop_x -= rtr_x_off
                        if flipy:
                            names = (f"x{named_x}y{named_y}.south", f"x{named_x}y{named_y - 1}.north")
                        else:
                            names = (f"x{named_x}y{named_y}.north", f"x{named_x}y{named_y + 1}.south")
                        s += self._draw_rtr_lines(start_x, start_y, stop_x, stop_y, names)
                else:
                    raise RuntimeError(f"rotate of {rotate} is not yet implemented")

        return s

    def _draw_rtr_lines(self, x1, y1, x2, y2, paths):
        if x1 == x2:
            return self._draw_rtr_line(x1 - 12, y1, x2 - 12, y2, paths[0]) + self._draw_rtr_line(
                x2 + 12, y2, x1 + 12, y1, paths[1]
            )
        else:
            return self._draw_rtr_line(x1, y1 - 12, x2, y2 - 12, paths[0]) + self._draw_rtr_line(
                x2, y2 + 12, x1, y1 + 12, paths[1]
            )

    def _draw_rtr_line(self, x1, y1, x2, y2, path):
        attrs = self.attrs.get(path, {})
        stroke = "black"
        fill = "black"
        if "color" in attrs:
            stroke = attrs["color"]
            fill = attrs["color"]
        stroke = attrs.get("stroke", stroke)
        fill = attrs.get("fill", fill)

        s = f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{stroke}" stroke-width="4"/>\n'

        if "label" in attrs:
            center_x = (x1 + x2) // 2
            center_y = (y1 + y2) // 2

            if x2 > x1:
                jog_x = (x2 - x1) // 6
            elif x2 < x1:
                jog_x = -((x1 - x2) // 6)
            else:
                jog_x = 0

            if y2 > y1:
                jog_y = (y2 - y1) // 6
            elif y2 < y1:
                jog_y = -((y1 - y2) // 6)
            else:
                jog_y = 0

            label = attrs["label"]
            id = path.replace(".", "-")

            s += f'<g id="{id}">\n'
            s += f'<circle cx="{center_x + jog_x}" cy="{center_y + jog_y}" r="12" stroke="black" fill="darkslategray"/>\n'
            s += f'<text x="{center_x + jog_x - 9}" y="{center_y + jog_y + 5}" stroke="white">{label}</text>\n'

            if "title" in attrs:
                s += f"<title>{attrs['title']}</title>\n"

            s += "</g>\n"

        if x1 == x2:
            if y2 < y1:  # ^
                s += f'<path d="M {x2} {y2} l -5 10 h 10 Z" fill="{fill}"/>\n'
            else:  # v
                s += f'<path d="M {x2} {y2} l -5 -10 h 10 Z" fill="{fill}"/>\n'
        elif y1 == y2:
            if x2 < x1:  # <
                s += f'<path d="M {x2} {y2} l 10 -5 v 10 Z" fill="{fill}"/>\n'
            else:  # >
                s += f'<path d="M {x2} {y2} l -10 -5 v 10 Z" fill="{fill}"/>\n'

        return s


class SoC(Element):
    """An SoC is an arrangement of Dies."""

    DIES = []

    def __init__(self, dies=None, **attrs):
        if dies is None:
            dies = self.DIES

        self.dies = []
        self.die_offsets = []
        self.die_rotation = []
        self.die_flipx = []
        self.die_flipy = []
        self.dev_by_type = defaultdict(lambda: [])
        self.rdi_route = defaultdict(lambda: [])
        self.rtr_by_xy = {}
        self.dev_by_xypd = {}
        max_x = 0
        max_y = 0

        for die_id, die_info in enumerate(dies):
            die_type = die_info["die_type"]
            x_off = die_info.get("x_off", 0)
            y_off = die_info.get("y_off", 0)
            die_attrs = attrs.get(f"die{die_id}", {})
            die = die_type(die_id, **die_attrs)
            self.dies.append(die)
            self.rtr_by_xy.update(die.rtr_by_xy)
            self.dev_by_xypd.update(die.dev_by_xypd)
            self._update_dev_by_type(die.dev_by_type)
            self.die_offsets.append((x_off, y_off))
            rotate = die_info.get("rotate", 0)
            self.die_rotation.append(rotate)
            flipx = die_info.get("flipx", False)
            self.die_flipx.append(flipx)
            flipy = die_info.get("flipy", False)
            self.die_flipy.append(flipy)

            if rotate in {90, -90}:
                die_width = die.height
                die_height = die.width
            else:
                die_width = die.width
                die_height = die.height

            if die_width + x_off * Tile.WIDTH > max_x:
                max_x = die_width + x_off * Tile.WIDTH

            if die_height + y_off * Tile.HEIGHT > max_y:
                max_y = die_height + y_off * Tile.HEIGHT

        self.width = max_x
        self.height = max_y
        self.attrs = attrs

    def _update_dev_by_type(self, new_dev_by_type):
        for k, v in new_dev_by_type.items():
            if k in self.dev_by_type:
                self.dev_by_type[k] += v
            else:
                self.dev_by_type[k] = v

    def devices(self):
        return sum(self.dev_by_type.values(), start=[])

    def connect_rdi(self, rdi1, rdi2):
        assert rdi1.die_id != rdi2.die_id

        rdi1.other = rdi2
        rdi2.other = rdi1

        self.rdi_route[(rdi1.die_id, rdi2.die_id)].append([rdi1])
        self.rdi_route[(rdi2.die_id, rdi1.die_id)].append([rdi2])

    def render(self, **attrs):
        s = ""

        for i, die in enumerate(self.dies):
            x_off, y_off = self.die_offsets[i]
            s += die.render(
                x=x_off * Tile.WIDTH,
                y=y_off * Tile.HEIGHT,
                rotate=self.die_rotation[i],
                flipx=self.die_flipx[i],
                flipy=self.die_flipy[i],
            )

        scale = attrs.pop("scale", 1.0)

        if "width" not in attrs:
            attrs["width"] = int(self.width * scale)

        if "height" not in attrs:
            attrs["height"] = int(self.height * scale)

        if scale is not None and "viewBox" not in attrs:
            attrs["viewBox"] = f"0 0 {int(attrs['width'] / scale)} {int(attrs['height'] / scale)}"

        return self.svg(s, **attrs)
