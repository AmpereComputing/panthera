# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
SVG render routines for SoC components.

This is involved enough that it made sense to separate it out from the
basic objects.
"""

import math
import re

from . import components
from .annotations import Annotations


def get_annotations(ann, k):
    try:
        if isinstance(k, components.Component):
            comp = k
            ann = ann[f"die{comp.die_id}"]
            ann = ann[f"x{comp.x}y{comp.y}"]
            if comp.port >= 0:
                ann = ann[f"port{comp.port}"]
            if comp.didx and comp.didx >= 0:
                ann = ann[f"dev{comp.didx}"]
        else:
            ks = k.split(".")
            while ks:
                k = ks.pop(0)
                ann = ann[k]
    except KeyError:
        return {}
    return ann


class Transforms:
    """Device ports are laid out like this:

    Normal     Flip X    Flip Y    Flip X&Y   Rotate -90  Rotate 90  Rotate 180
     0 | 1      1 | 0     3 | 2     2 | 3       1 | 2       3 | 0      2 | 3
     --+--      --+--     --+--     --+--       --+--       --+--      --+--
     3 | 2      2 | 3     0 | 1     1 | 0       0 | 3       2 | 1      1 | 0
    """

    def __init__(self, rotate=0, flipx=False, flipy=False):
        assert rotate in {0, 90, -90, 180}
        self.rotate = rotate
        self.flipx = flipx
        self.flipy = flipy

    def port(self, port):
        if self.flipx and self.flipy:
            port = {0: 2, 1: 3, 2: 0, 3: 1}[port]
        elif self.flipx:
            port = {0: 1, 1: 0, 2: 3, 3: 2}[port]
        elif self.flipy:
            port = {0: 3, 1: 2, 2: 1, 3: 0}[port]

        if self.rotate == 90:
            port = 3 if port == 0 else port - 1
        elif self.rotate == -90:
            port = 0 if port == 3 else port + 1
        elif self.rotate == 180:
            port = {0: 2, 1: 3, 2: 0, 3: 1}[port]
        elif self.rotate == 0:
            pass
        else:
            raise RuntimeError(f"rotate {self.rotate} is not yet implemented")

        return port


def port_is_upper(port):
    """Ports 0 and 1 are above the RTR, 2 and 3 are below.

    Like this:

        0 | 1
        --+--
        3 | 2

    We need to know which hemisphere we're in so we can flip some elements
    (like the position of a CPX) appropriately.
    """
    return port in {0, 1}


class SVGElement:
    """Base class for all SVG elements."""

    def __init__(self, *items, **attrs):
        self.tag = attrs.pop("tag", None)
        self.items = [*items]
        self.inner = None
        self.attrs = attrs

    def path(self, **attrs):
        return SVGElement(tag="path", **attrs)

    def line(self, **attrs):
        return SVGElement(tag="line", **attrs)

    def rect(self, **attrs):
        return SVGElement(tag="rect", **attrs)

    def circle(self, **attrs):
        return SVGElement(tag="circle", **attrs)

    def polygon(self, **attrs):
        return SVGElement(tag="polygon", **attrs)

    def g(self, *items, **attrs):
        elem = SVGElement(tag="g", **attrs)
        elem.items = [*items]
        return elem

    def text(self, inner, **attrs):
        elem = SVGElement(tag="text", **attrs)
        elem.inner = str(inner)
        return elem

    def title(self, inner, **attrs):
        elem = SVGElement(tag="title", **attrs)
        elem.inner = str(inner)
        return elem

    def translate(self, x, y):
        return self.g(self, transform=f"translate({x}, {y})")

    def rotate(self, deg, cx=0, cy=0):
        return self.g(self, transform=f"rotate({deg}, {cx}, {cy})")

    def append(self, new_item):
        self.items.append(new_item)

    def __str__(self):
        if self.tag is not None:
            attr_str = " ".join([f'{k}="{v}"' for k, v in self.attrs.items()])
            s = f"<{self.tag} {attr_str}>"
        else:
            s = ""

        if self.items is not None:
            if isinstance(self.items, list):
                for item in self.items:
                    s += str(item)
            else:
                s += str(self.items)

        if self.inner:
            assert self.tag
            s += self.inner

        if self.tag is not None:
            s += f"</{self.tag}>\n"

        return s


class RTR(SVGElement):
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

    SIDE = 50
    WIDTH = 50 + 35 * 2
    HEIGHT = 50 + 35 * 2
    FILL = "#dbeef3"
    STROKE = "black"

    def __init__(self, renderer, rtr, annotations):
        super().__init__()
        ann = get_annotations(annotations, rtr)
        fill = ann.get("fill", self.FILL)
        stroke = ann.get("stroke", self.STROKE)
        self.width = self.WIDTH
        self.height = self.HEIGHT
        center_x = self.WIDTH / 2
        center_y = self.HEIGHT / 2
        self.items = self.g(
            self.path(d="M 35 0 h 50 l 35 35 v 50 l -35 35 h -50 l -35 -35 v -50 Z", fill=fill, stroke=stroke),
            self.text(f"{rtr.x},{rtr.y}", x=center_x, y=center_y),
        )


class CpxComplex(SVGElement):
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

    def __init__(self, renderer, cpx, annotations, transforms):
        super().__init__()
        self.items = self.g()
        ann = get_annotations(annotations, cpx)
        self.width = cpx.n * self.WIDTH
        self.height = self.HEIGHT + self.CPX_HEIGHT

        port = transforms.port(cpx.port)
        y = 0 if port_is_upper(port) else self.CPX_HEIGHT

        for i in range(cpx.n):
            stroke, fill = self._get_stroke_and_fill(cpx.devices[i], ann[f"dev{i}"])
            self.items.append(
                self.rect(width=self.WIDTH, x=self.WIDTH * i, y=y, height=self.HEIGHT, stroke=stroke, fill=fill)
            )

        for i in range(cpx.n):
            dev_attrs = ann[f"dev{i}"]

            if "label" in dev_attrs:
                label = dev_attrs["label"]

                center_x = (
                    (self.WIDTH * i) + (self.WIDTH // 2) - (len(label) // 2) * 8
                )  # not sure how to adjust this text
                center_y = y + self.HEIGHT // 2
                self.items.append(self.text(label, x=center_x, y=center_y, stroke="black"))

        y = self.HEIGHT if port_is_upper(port) else 0
        self.items.append(
            self.rect(y=y, width=self.width, height=self.CPX_HEIGHT, stroke=self.STROKE, fill=self.CPX_FILL)
        )

        for i in range(cpx.n):
            dev_attrs = ann[f"from_dev{i}"]
            if "label" in dev_attrs:
                label = dev_attrs["label"]
                x0 = self.WIDTH * i
                y0 = self.HEIGHT if port_is_upper(port) else (self.CPX_HEIGHT)
                x1 = self.WIDTH * i + self.WIDTH
                y1 = self.HEIGHT if port_is_upper(port) else (self.CPX_HEIGHT)
                x2 = self.WIDTH * i + (self.WIDTH // 2)
                y2 = (self.HEIGHT + self.CPX_HEIGHT) if port_is_upper(port) else 0
                text_y = y0 + 12 if port_is_upper(port) else y0 - 4
                link_label = self.g(
                    self.polygon(points=f"{x0},{y0} {x1},{y1} {x2},{y2}", stroke="black", fill="darkslategray"),
                    self.text(label, x=x0 + 9, y=text_y, stroke="white"),
                )

                self.items.append(link_label)

        for i in range(cpx.n):
            dev_attrs = ann[f"to_dev{i}"]
            if "label" in dev_attrs:
                label = dev_attrs["label"]
                x0 = self.WIDTH * i
                y0 = self.HEIGHT if port_is_upper(port) else (self.CPX_HEIGHT)
                x1 = self.WIDTH * i + self.WIDTH
                y1 = self.HEIGHT if port_is_upper(port) else (self.CPX_HEIGHT)
                x2 = self.WIDTH * i + (self.WIDTH // 2)
                y2 = (self.HEIGHT - self.CPX_HEIGHT) if port_is_upper(port) else 2 * self.CPX_HEIGHT
                text_y = y0 - 4 if port_is_upper(port) else y0 + 12
                link_label = self.g(
                    self.polygon(points=f"{x0},{y0} {x1},{y1} {x2},{y2}", stroke="black", fill="darkslategray"),
                    self.text(label, x=x0 + 9, y=text_y, stroke="white"),
                )

                self.items.append(link_label)

    def _get_stroke_and_fill(self, obj, ann):
        """Determine the stroke and fill colors for this object.

        Start with class settings on the object as default, but allow the
        annotations to override if they are set.

        Note that "color" is a shorthand for setting both stroke and fill
        together, if that's more convenient.
        """
        fill = obj.FILL if hasattr(obj, "FILL") else self.FILL
        stroke = obj.STROKE if hasattr(obj, "SROKE") else self.STROKE

        if "color" in ann:
            stroke = ann["color"]
            fill = ann["color"]

        stroke = ann.get("stroke", stroke)
        fill = ann.get("fill", fill)

        return stroke, fill


class GenericDevice(SVGElement):
    """A simple rectangle representing a generic device; same size as
    the device in a CpxComplex.

    Construction:

        GenericDevice(die_id, x, y, **attrs)

        Required:
            x                X coordinate of the router this RDI is attached to
            y                Y coordinate of the router this RDI is attached to
            port             Which device port (0, 1, 2, 3) this RDI is attached to

        Optional:
            fill=<color>     Fill color
            stroke=<color>   Stroke color
    """

    WIDTH = 35
    HEIGHT = 75
    STROKE = "black"
    FILL = "#d2d200"

    def __init__(self, renderer, comp, annotations):
        super().__init__()
        self.width = getattr(comp, "WIDTH", self.WIDTH)
        self.height = getattr(comp, "HEIGHT", self.HEIGHT)
        ann = get_annotations(annotations, comp)
        fill = ann.get("fill", getattr(comp, "FILL", self.FILL))
        stroke = ann.get("stroke", getattr(comp, "STROKE", self.STROKE))
        self.items = self.rect(width=self.width, height=self.height, fill=fill, stroke=stroke)


class RDI(SVGElement):
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

    WIDTH = 100
    HEIGHT = 70
    STROKE = "black"
    FILL = "#923931"

    def __init__(self, renderer, rdi, annotations):
        super().__init__()
        self.width = self.WIDTH
        self.height = self.HEIGHT
        ann = get_annotations(annotations, rdi)
        fill = ann.get("fill", self.FILL)
        stroke = ann.get("stroke", self.STROKE)
        self.items = self.rect(width=self.WIDTH, height=self.HEIGHT, fill=fill, stroke=stroke)


class COM(SVGElement):
    """A simple rectangle representing a COM device.

    Construction:

        COM(die_id, x, y, **attrs)

        Required:
            x                X coordinate of the router this RDI is attached to
            y                Y coordinate of the router this RDI is attached to
            port             Which device port (0, 1, 2, 3) this RDI is attached to

        Optional:
            fill=<color>     Fill color
            stroke=<color>   Stroke color
    """

    WIDTH = 100
    HEIGHT = 70
    STROKE = "black"
    FILL = "#ab9ac0"

    def __init__(self, renderer, rdi, annotations):
        super().__init__()
        self.width = self.WIDTH
        self.height = self.HEIGHT
        ann = get_annotations(annotations, rdi)
        fill = ann.get("fill", self.FILL)
        stroke = ann.get("stroke", self.STROKE)
        self.items = self.rect(width=self.WIDTH, height=self.HEIGHT, fill=fill, stroke=stroke)


class ACR(SVGElement):
    """A simple rectangle representing a flavor of ACR device.

    Construction:

        ACR(die_id, x, y, **attrs)

        Required:
            x                X coordinate of the router this RDI is attached to
            y                Y coordinate of the router this RDI is attached to
            port             Which device port (0, 1, 2, 3) this RDI is attached to

        Optional:
            fill=<color>     Fill color
            stroke=<color>   Stroke color
    """

    WIDTH = 100
    HEIGHT = 70
    STROKE = "black"
    FILL = "#ffff66"

    def __init__(self, renderer, rdi, annotations):
        super().__init__()
        self.width = self.WIDTH
        self.height = self.HEIGHT
        ann = get_annotations(annotations, rdi)
        fill = ann.get("fill", self.FILL)
        stroke = ann.get("stroke", self.STROKE)
        self.items = self.rect(width=self.WIDTH, height=self.HEIGHT, fill=fill, stroke=stroke)


class CCL_ACR_SYS(ACR):
    pass


class CCL_ACR_GIC(ACR):
    pass


class CCL_ACR_CTRACE(ACR):
    pass


class CCL_ACR_MTRACE(ACR):
    pass


class CCL_ACR_DDR(ACR):
    pass


class IOD_ACR_SYS(ACR):
    pass


class IOD_ACR_GIC(ACR):
    pass


class IOD_ACR_MTRACE(ACR):
    pass


class IOD_ACR_SMMU(ACR):
    pass


class Tile(SVGElement):
    """A Tile holds a router and optional devices around it.

    The Tile is drawn in a 400x400 box with a RTR in the middle. If a device list is given,
    it is interpreted as a device per port where the port is the list index. So for example:

        Tile(0, 0, devices=[DevOnPort0, DevOnPort1, None, None])

    would produce a Tile at X=0, Y=0 with two devices attached; one at port 0 and one at
    port 1 (the upper two ports), while ports 2 and 3 are empty. The devices are rendered
    in the quadrants around the router:

         0 | 1
        ---+---
         3 | 2

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

    PORT_OFFSETS = [(100, 10), (300, 10), (300, 290), (100, 290)]

    def __init__(self, renderer, tile, annotations, transforms):
        super().__init__()
        self.items = self.g()

        self.width = self.WIDTH
        self.height = self.HEIGHT

        center_x = self.WIDTH / 2
        center_y = self.HEIGHT / 2

        rtr = renderer.render(tile.rtr, annotations)
        self.items.append(rtr.translate(x=center_x - rtr.width / 2, y=center_y - rtr.height / 2))

        for port, dev in enumerate(tile.devices):
            if dev is None:
                continue

            dev_link_attrs_in = get_annotations(annotations, f"die{tile.die_id}.x{tile.x}y{tile.y}.port{port}.to_rtr")
            dev_link_attrs_out = get_annotations(annotations, f"die{tile.die_id}.x{tile.x}y{tile.y}.port{port}.to_dev")

            port = transforms.port(port)

            x_port_off, y_port_off = Tile.PORT_OFFSETS[port]
            d = renderer.render(dev, annotations, transforms)
            d = d.translate(x=x_port_off - d.width / 2, y=y_port_off)
            self.items.append(d)

            link_x, link_y, label_in_x, label_in_y, label_out_x, label_out_y, rot = self._link_coords_by_port(
                port, rtr, center_x, center_y
            )

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
            dev_links = (
                self.g(
                    self.path(d="M 100 100 l -5 -10 h 10 Z", fill=fill_in),
                    self.line(**{"x1": 100, "y1": 95, "x2": 100, "y2": 65, "stroke": stroke_in, "stroke-width": 4}),
                    self.path(d="M 125 65 l -5 10 h 10 Z", fill=fill_out),
                    self.line(**{"x1": 125, "y1": 70, "x2": 125, "y2": 100, "stroke": stroke_out, "stroke-width": 4}),
                )
                .rotate(rot, cx=100, cy=100)
                .translate(link_x - 100, link_y - 100)
            )

            self.items.append(dev_links)

            if "label" in dev_link_attrs_in:
                self._add_label(dev_link_attrs_in, label_in_x, label_in_y)

            if "label" in dev_link_attrs_out:
                self._add_label(dev_link_attrs_out, label_out_x, label_out_y)

    @staticmethod
    def _link_coords_by_port(port, rtr, center_x, center_y):
        inset = 8  # how far in from the octagon's vertices to draw the link arrows

        if port == 0:
            link_x = center_x - (rtr.WIDTH / 2) + inset
            link_y = center_y - (rtr.SIDE / 2) - inset
            label_in_x = link_x - 25
            label_in_y = link_y - 25
            label_out_x = link_x + 18
            label_out_y = link_y - 18
            rot = -45
        elif port == 1:
            link_x = center_x + (rtr.SIDE / 2) + inset
            link_y = center_y - (rtr.HEIGHT / 2) + inset
            label_in_x = link_x + 25
            label_in_y = link_y - 25
            label_out_x = link_x + 18
            label_out_y = link_y + 18
            rot = 45
        elif port == 2:
            link_x = center_x + (rtr.WIDTH / 2) - inset
            link_y = center_y + (rtr.SIDE / 2) + inset
            label_in_x = link_x + 25
            label_in_y = link_y + 25
            label_out_x = link_x - 18
            label_out_y = link_y + 18
            rot = 135
        elif port == 3:
            link_x = center_x - (rtr.SIDE / 2) - inset
            link_y = center_y + (rtr.HEIGHT / 2) - inset
            label_in_x = link_x - 25
            label_in_y = link_y + 25
            label_out_x = link_x - 18
            label_out_y = link_y - 18
            rot = -135

        return link_x, link_y, label_in_x, label_in_y, label_out_x, label_out_y, rot

    def _add_label(self, link_attrs, label_x, label_y):
        label = link_attrs["label"]

        link_label = self.g(
            self.circle(cx=label_x, cy=label_y, r=12, stroke="black", fill="darkslategray"),
            self.text(label, x=label_x - 9, y=label_y + 5, stroke="white"),
        )

        if "title" in link_attrs:
            link_label.append(self.title(link_attrs["title"]))

        self.items.append(link_label)


class Die(SVGElement):
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

    def __init__(self, renderer, die, annotations, transforms):
        super().__init__()

        self.ann = get_annotations(annotations, f"die{die.die_id}")
        self.items = self.g()

        if transforms.rotate in {90, -90}:
            self.width = Tile.HEIGHT * die.rows
            self.height = Tile.WIDTH * die.cols
            self._render_grid_lines(Tile.HEIGHT, Tile.WIDTH, die.cols, die.rows)
        else:
            self.width = Tile.WIDTH * die.cols
            self.height = Tile.HEIGHT * die.rows
            self._render_grid_lines(Tile.WIDTH, Tile.HEIGHT, die.rows, die.cols)

        self._render_tiles(die, renderer, annotations, transforms)
        self._render_rtr_connections(die, transforms)

    def _render_grid_lines(self, tile_width, tile_height, rows, cols):
        """Draw the dashed grid lines that appear between all tiles."""
        for x in [tile_width * i for i in range(cols + 1)]:
            self.items.append(
                self.line(
                    **{
                        "x1": x,
                        "y1": 0,
                        "x2": x,
                        "y2": tile_height * rows,
                        "stroke": "black",
                        "stroke-dasharray": "4,4",
                        "stroke-dashoffset": 2,
                    }
                )
            )

        for y in [tile_height * i for i in range(rows + 1)]:
            self.items.append(
                self.line(
                    **{
                        "x1": 0,
                        "y1": y,
                        "x2": tile_width * cols,
                        "y2": y,
                        "stroke": "black",
                        "stroke-dasharray": "4,4",
                        "stroke-dashoffset": 2,
                    }
                )
            )

    def _render_tiles(self, die, renderer, annotations, transforms):
        for x in range(die.cols):
            for y in range(die.rows):
                tile = die.tiles[x][y]
                gx, gy = self._tile_xy(die, x, y, transforms)
                self.items.append(renderer.render(tile, annotations, transforms).translate(x=gx, y=gy))

    def _tile_xy(self, die, x, y, transforms):
        """Determine the actual position of the upper left hand corner of a tile,
        taking into account flipping and rotation.
        """
        if transforms.flipx:
            x = die.cols - x - 1

        if not transforms.flipy:
            y = die.rows - y - 1

        if transforms.rotate == -90:
            tx = y * Tile.HEIGHT
            ty = (die.cols - x - 1) * Tile.WIDTH
        elif transforms.rotate == 90:
            ty = x * Tile.WIDTH
            tx = (die.rows - y - 1) * Tile.HEIGHT
        elif transforms.rotate == 0:
            tx = x * Tile.WIDTH
            ty = y * Tile.HEIGHT
        elif transforms.rotate == 180:
            tx = (die.cols - x - 1) * Tile.WIDTH
            ty = (die.rows - y - 1) * Tile.HEIGHT
        else:
            raise RuntimeError(f"rotate={transforms.rotate} is not yet implemented")

        return tx, ty

    def _rtr_xy(self, die, x, y, transforms):
        """Determine the actual position of the center of the router in a tile.
        This starts by getting the tile coordinates and then offsetting those
        to reach the center.
        """
        tx, ty = self._tile_xy(die, x, y, transforms)

        if transforms.rotate in {90, -90}:
            return tx + Tile.HEIGHT / 2, ty + Tile.WIDTH / 2
        else:
            return tx + Tile.WIDTH / 2, ty + Tile.HEIGHT / 2

    def _render_rtr_connections(self, die, transforms):
        for x in range(die.cols):
            for y in range(die.rows):
                if x > 0:
                    if ((x - 1, y), (x, y)) not in die.disconnects:
                        # Draw east/west connections between this tile and the one to
                        # our west (since x is greather than 0). These connections are
                        # always logically east/west even if the die has been transformed.
                        #
                        ex, ey = self._rtr_xy(die, x, y, transforms)
                        wx, wy = self._rtr_xy(die, x - 1, y, transforms)
                        self._render_rtr_lines(ex, ey, wx, wy, (f"x{x}y{y}.west", f"x{x - 1}y{y}.east"))

                if y > 0:
                    if ((x, y - 1), (x, y)) not in die.disconnects:
                        # Draw north/south connections between this tile and the one to
                        # our south.
                        #
                        nx, ny = self._rtr_xy(die, x, y, transforms)
                        sx, sy = self._rtr_xy(die, x, y - 1, transforms)
                        self._render_rtr_lines(nx, ny, sx, sy, (f"x{x}y{y}.south", f"x{x}y{y - 1}.north"))

    def _render_rtr_lines(self, x1, y1, x2, y2, paths):
        """Each router link is a pair of lines, one going each direction:

             --->  or  ^ |
             <---      | v

        NOTE: I'm being fast and loose with router width and height here.
        Since we might have been transformed, technically they should be
        swapped for some cases. But currently the router is symmetrical
        and so it doesn't matter.
        """
        if x1 == x2:
            if y1 < y2:
                y1 += RTR.HEIGHT / 2
                y2 -= RTR.HEIGHT / 2
            else:
                y1 -= RTR.HEIGHT / 2
                y2 += RTR.HEIGHT / 2
            self._render_rtr_line(x1 - 12, y1, x2 - 12, y2, paths[0])
            self._render_rtr_line(x2 + 12, y2, x1 + 12, y1, paths[1])
        else:
            if x1 < x2:
                x1 += RTR.WIDTH / 2
                x2 -= RTR.WIDTH / 2
            else:
                x1 -= RTR.WIDTH / 2
                x2 += RTR.WIDTH / 2
            self._render_rtr_line(x1, y1 - 12, x2, y2 - 12, paths[0])
            self._render_rtr_line(x2, y2 + 12, x1, y1 + 12, paths[1])

    def _render_rtr_line(self, x1, y1, x2, y2, path):
        ann = get_annotations(self.ann, path)
        stroke = "black"
        fill = "black"
        if "color" in ann:
            stroke = ann["color"]
            fill = ann["color"]
        stroke = ann.get("stroke", stroke)
        fill = ann.get("fill", fill)

        self.items.append(self.line(**{"x1": x1, "y1": y1, "x2": x2, "y2": y2, "stroke": stroke, "stroke-width": 4}))

        if "label" in ann:
            self.items.append(self._make_link_ann(x1, y1, x2, y2, ann, path))

        if x1 == x2:
            if y2 < y1:  # ^
                arrowhead = self.path(d=f"M {x2} {y2} l -5 10 h 10 Z", fill=fill)
            else:  # v
                arrowhead = self.path(d=f"M {x2} {y2} l -5 -10 h 10 Z", fill=fill)
        elif y1 == y2:
            if x2 < x1:  # <
                arrowhead = self.path(d=f"M {x2} {y2} l 10 -5 v 10 Z", fill=fill)
            else:  # >
                arrowhead = self.path(d=f"M {x2} {y2} l -10 -5 v 10 Z", fill=fill)

        self.items.append(arrowhead)

    def _make_link_ann(self, x1, y1, x2, y2, ann, path):
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

        label = ann["label"]
        id = path.replace(".", "-")

        link_ann = self.g(
            self.circle(cx=center_x + jog_x, cy=center_y + jog_y, r=12, stroke="black", fill="darkslategray"),
            self.text(label, x=center_x + jog_x - 9, y=center_y + jog_y + 5, stroke="white"),
            id=id,
        )

        if "title" in ann:
            link_ann.append(self.title(ann["title"]))

        return link_ann

    LOC_RE = re.compile(r"x([0-9]+)y([0-9]+)(?:\.port([0-9]+))?")

    def get_point(self, location, die, transforms):
        m = Die.LOC_RE.match(location)
        if not m:
            print(f"location {location} did not match")
            assert m
        x = int(m.group(1))
        y = int(m.group(2))
        port = int(m.group(3)) if m.group(3) else None
        if port is None:
            return self._rtr_xy(die, x, y, transforms)
        else:
            x, y = self._rtr_xy(die, x, y, transforms)
            port = transforms.port(port)
            x += [-100, 100, 100, -100][port]
            y += [-100, -100, 100, 100][port]
            return x, y


class SoC(SVGElement):
    """An SoC is an arrangement of Dies."""

    def __init__(self, renderer, soc, annotations, transforms):
        super().__init__()

        max_x = 0
        max_y = 0

        self.items = self.g()
        renderer_index = {}

        for die in soc.dies:
            if die is None:
                continue

            die_transforms = Transforms(rotate=die.rotate, flipx=die.flipx, flipy=die.flipy)
            d = renderer.render(die, annotations, die_transforms)
            renderer_index[die.die_id] = (d, die, die_transforms)

            if d.width + die.x_off * Tile.WIDTH > max_x:
                max_x = d.width + die.x_off * Tile.WIDTH

            if d.height + die.y_off * Tile.HEIGHT > max_y:
                max_y = d.height + die.y_off * Tile.HEIGHT

            d = d.translate(die.x_off * Tile.WIDTH, die.y_off * Tile.HEIGHT)
            self.items.append(d)

        self._render_paths(annotations, renderer_index)
        self.width = max_x
        self.height = max_y

    PATH_RE = re.compile(rf"die([0-9]+)\.(.*)")

    def _render_paths(self, annotations, renderer_index):
        if "paths" in annotations:
            for path in annotations["paths"]:
                color = path.get("color", "orange")
                opacity = path.get("opacity", 0.5)
                width = path.get("width", 10)
                self._render_path(path["points"], renderer_index, color, opacity, width)

    def _render_path(self, path, renderer_index, color, opacity, width):
        points = []
        for segment in path:
            m = SoC.PATH_RE.match(segment)
            if m:
                die_id = int(m.group(1))
                d, die, die_transforms = renderer_index[die_id]
                x, y = d.get_point(m.group(2), die, die_transforms)
                x += die.x_off * Tile.WIDTH
                y += die.y_off * Tile.HEIGHT
                points.append((x, y))

        assert len(points) >= 2

        path_d = f"M {points[0][0]} {points[0][1]} " + " ".join([f"L {x} {y}" for x, y in points[1:]])
        path_d += self._draw_arrowhead(points[-2], points[-1], width)

        attrs = {"d": path_d, "stroke": color, "stroke-width": width, "opacity": opacity, "fill": "none"}

        self.items.append(self.path(**attrs))

    def _draw_arrowhead(self, p1, p2, width):
        l = 50
        s = int(l * math.sqrt(2))

        x1, y1 = p1
        x2, y2 = p2

        if x1 == x2:
            y_off = s if y2 < y1 else -s
            return f" M {x2} {y2} L {x2 - s} {y2 + y_off} M {x2} {y2} L {x2 + s} {y2 + y_off}"
        elif y1 == y2:
            x_off = s if x2 < x1 else -s
            return f" M {x2} {y2} L {x2 + x_off} {y2 + s} M {x2} {y2} L {x2 + x_off} {y2 - s}"
        elif x2 > x1 and y2 > y1:
            return f" M {x2 + width // 2} {y2} L {x2 - l} {y2} M {x2} {y2} L {x2} {y2 - l}"
        elif x2 < x1 and y2 < y1:
            return f" M {x2} {y2 - width // 2} L {x2} {y2 + l} M {x2} {y2} L {x2 + l} {y2}"
        elif x2 > x1 and y2 < y1:
            return f" M {x2 + width // 2} {y2} L {x2 - l} {y2} M {x2} {y2} L {x2} {y2 + l}"
        elif x2 < x1 and y2 > y1:
            return f" M {x2} {y2 + width // 2} L {x2} {y2 - l} M {x2} {y2} L {x2 + l} {y2}"


class SVGRenderer:
    def render(self, comp, annotations=None, transforms=None):
        if annotations is None:
            annotations = Annotations()
        if transforms is None:
            transforms = Transforms()
        if isinstance(comp, components.SoC):
            return SoC(self, comp, annotations, transforms)
        if isinstance(comp, components.Die):
            return Die(self, comp, annotations, transforms)
        if isinstance(comp, components.Tile):
            return Tile(self, comp, annotations, transforms)
        elif isinstance(comp, components.CpxComplex):
            return CpxComplex(self, comp, annotations, transforms)
        elif isinstance(comp, components.Router):
            return RTR(self, comp, annotations)
        elif isinstance(comp, components.ADI):
            return RDI(self, comp, annotations)
        elif isinstance(comp, components.COM):
            return COM(self, comp, annotations)
        elif isinstance(
            comp,
            (
                components.CCL_ACR_SYS,
                components.CCL_ACR_GIC,
                components.CCL_ACR_CTRACE,
                components.CCL_ACR_MTRACE,
                components.CCL_ACR_DDR,
                components.IOD_ACR_SYS,
                components.IOD_ACR_GIC,
                components.IOD_ACR_MTRACE,
                components.IOD_ACR_SMMU,
            ),
        ):
            return ACR(self, comp, annotations)
        else:
            return GenericDevice(self, comp, annotations)

    def __call__(self, comp, annotations=None, transforms=None, scale=1.0):
        r = self.render(comp, annotations, transforms)
        w = r.width * scale
        h = r.height * scale
        vw = int(w / scale)
        vh = int(h / scale)
        return f'<svg viewBox="0 0 {vw} {vh}" width="{w}" height="{h}">{r}</svg>'
