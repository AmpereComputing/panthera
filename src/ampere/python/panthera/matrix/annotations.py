# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


class Annotations:
    """Annotations is a container for extra data that "annotates" an SVG rendering
    of an SoC or Die. This can refer to labels on links (such as links between
    routers), text inside device boxes, or even pathways through the SoC drawn as
    large arrows.

    The rendering code decides what to *do* with the annotation. This class is just
    a way of containing the information in an organized way. It acts more or less
    like a hierarchical dictionary that can also be access via attributes (if the
    keys are valid Python attribute names, of course).

    For example, to add a label to a particular device on an SoC, you might set
    a value on an Annotations object like this:

        ann = Annotations()
        ann.die0.x0y0.port1.label = 'Core 0'

    which could also be accessed like this:

        ann['die0']['x0y0']['port1']['label']

    The object at each level is its own `Annotations`; that is, `ann['die0']` is
    an `Annotations` object just like `ann` itself is. Such child objects are
    created automatically when you access those attributes if the keys are not
    already set.
    """

    def __init__(self):
        self._data = {}

    def __contains__(self, item):
        return item in self._data

    def __getattr__(self, item):
        if item.startswith("_"):
            return super().__getattr__(self, item)
        else:
            return self.__getitem__(item)

    def __setattr__(self, item, value):
        if item.startswith("_"):
            super().__setattr__(item, value)
        else:
            self.__setitem__(item, value)

    def __getitem__(self, item):
        if item not in self._data:
            self._data[item] = Annotations()
        return self._data[item]

    def __setitem__(self, item, value):
        self._data[item] = value

    def get(self, item, default=None):
        return self._data.get(item, default)

    def items(self):
        return self._data.items()

    def keys(self):
        return self._data.keys()

    def values(self):
        return self._data.values()

    @staticmethod
    def _expand_dict(o):
        if isinstance(o, (Annotations, dict)):
            return {k: Annotations._expand_dict(v) for k, v in o.items()}
        else:
            return o

    def __repr__(self):
        return self._expand_dict(self._data).__repr__()


def path_to_annotation(ann, path):
    """Turn a dot-separated path into a Annotation reference."""
    path = path.split(".")
    while path:
        ann = ann[path.pop(0)]
    return ann


def util_to_annotation(util, chan, colormap, absolute=True):
    """Translate the utilization DataFrame into Annotations.

    The utilization % is put into a link label as well as being mapped to a color through
    the given colormap (a heatmap, really).
    """
    util = util[[chan, f"{chan}_title"]].dropna()
    ann = Annotations()
    m = 100.0 if absolute else util[chan].max()

    for (die_id, link), row in util.iterrows():
        ann_die = ann[f"die{die_id}"]
        value = row[chan]
        title = row[f"{chan}_title"]
        label = f"{value:0.1f}" if value < 10.0 else f"{value:0.0f}"
        r, g, b, _ = colormap(value / m, bytes=True)

        a = path_to_annotation(ann_die, link)

        a.color = f"rgb({r} {g} {b})"
        a.label = label
        if title:
            a.title = title

    return ann


def ooc_to_annotation(util, chan, colormap):
    """Translate the Out-Of-Credit DataFrame into Annotations."""
    util = util[[chan]].dropna()
    ann = Annotations()
    for (die_id, link), row in util.iterrows():
        ann_die = ann[f"die{die_id}"]
        value = row[chan]
        label = f"{value:0.1f}" if value < 10.0 else f"{value:0.0f}"
        dev = link.split(".")
        if len(dev) > 2 and re.match(r".*to_dev(\d+)", dev[2]):
            a = path_to_annotation(ann_die, link)
            a.label = label
        elif len(dev) > 2 and re.match(r".*from_dev(\d+)", dev[2]):
            a = path_to_annotation(ann_die, link)
            a.label = label
        else:
            r, g, b, _ = colormap(value / 100.0, bytes=True)
            a = path_to_annotation(ann_die, link)
            a.color = f"rgb({r} {g} {b})"
            a.label = label
    return ann


def both_to_annotation(util, ooc, chan, colormap):
    """Translate both the Utilization and Out-Of-Credit DataFrames into Annotations."""
    ooc = ooc[[chan]].dropna()
    util = util[[chan]].dropna()
    ann = Annotations()

    for (die_id, link), ooc_row in ooc.iterrows():
        ann_die = ann[f"die{die_id}"]
        try:
            util_value = util.loc[die_id].loc[link][chan]
        except KeyError:
            util_value = 0

        value = ooc_row[chan] + util_value
        label = f"{value:0.1f}" if value < 10.0 else f"{value:0.0f}"
        r, g, b, _ = colormap(value / 100.0, bytes=True)
        a = path_to_annotation(ann_die, link)
        a.color = f"rgb({r} {g} {b})"
        a.label = label
    return ann


def accumulator_to_annotations(accumulator, channel, ann):
    """Convert traffic accumulator information into Annotations."""

    def dir_name(from_comp, to_comp):
        if from_comp.y == to_comp.y:
            if from_comp.x < to_comp.x:
                return "east"
            elif from_comp.x > to_comp.x:
                return "west"
            else:
                raise RuntimeError(f"from_comp ({from_comp}) and to_comp ({to_comp}) are co-located!")
        elif from_comp.x == to_comp.x:
            if from_comp.y < to_comp.y:
                return "north"
            elif from_comp.y > to_comp.y:
                return "south"
            else:
                raise RuntimeError(f"from_comp ({from_comp}) and to_node ({to_comp}) are co-located!")
        else:
            raise RuntimeError(f"from_comp ({from_comp}) and to_node ({to_comp}) are not adjacent!")

    def acc_link(link, traffic, opcode):
        if "label" not in link:
            link["label"] = 0.0
        link["label"] += traffic
        traffic_by_opcode = link["traffic_by_opcode"]
        if opcode not in traffic_by_opcode:
            traffic_by_opcode[opcode] = 0.0
        traffic_by_opcode[opcode] += traffic

    def acc(from_comp, to_comp, traffic, opcode):
        acc_traffic = ann[f"die{from_comp.die_id}"]
        if from_comp.port == -1 and to_comp.port != -1:
            link = acc_traffic[f"x{from_comp.x}y{from_comp.y}"][f"port{to_comp.port}"]["to_dev"]
            acc_link(link, traffic, opcode)
        elif from_comp.port != -1 and to_comp.port == -1:
            link = acc_traffic[f"x{from_comp.x}y{from_comp.y}"][f"port{from_comp.port}"]["to_rtr"]
            acc_link(link, traffic, opcode)
        else:
            link = acc_traffic[f"x{from_comp.x}y{from_comp.y}"][dir_name(from_comp, to_comp)]
            acc_link(link, traffic, opcode)

    for (from_comp, to_comp), per_opcode_traffic in accumulator.as_dict(channel).items():
        if from_comp.die_id == to_comp.die_id:
            for opcode, traffic in per_opcode_traffic.items():
                acc(from_comp, to_comp, traffic, opcode)


def cycle_to_annotations(cycle):
    """Convert a cycle (which is a list of graph edges) into an annotation
    dictionary we can use to display highlighted links in the Die diagram.
    """
    cycle = cycle.copy()
    ann = Annotations()

    def dir_name(from_comp, to_comp):
        if from_comp.y == to_comp.y:
            if from_comp.x < to_comp.x:
                return "east"
            elif from_comp.x > to_comp.x:
                return "west"
            else:
                raise RuntimeError(f"from_comp ({from_comp}) and to_comp ({to_comp}) are co-located!")
        elif from_comp.x == to_comp.x:
            if from_comp.y < to_comp.y:
                return "north"
            elif from_comp.y > to_comp.y:
                return "south"
            else:
                raise RuntimeError(f"from_comp ({from_comp}) and to_node ({to_comp}) are co-located!")
        else:
            raise RuntimeError(f"from_comp ({from_comp}) and to_node ({to_comp}) are not adjacent!")

    def annotate(from_comp, to_comp, idx):
        die_id = from_comp.die_id

        if from_comp.port == -1 and to_comp.port >= 0:
            link = ann[f"die{die_id}"][f"x{from_comp.x}y{from_comp.y}"][f"port{to_comp.port}"]["to_dev"]
        elif from_comp.port >= 0 and to_comp.port == -1:
            link = ann[f"die{die_id}"][f"x{from_comp.x}y{from_comp.y}"][f"port{from_comp.port}"]["to_rtr"]
        else:
            link = ann[f"die{die_id}"][f"x{from_comp.x}y{from_comp.y}"][dir_name(from_comp, to_comp)]

        link["color"] = "red"
        link["label"] = idx

    idx = 1

    from_comp = cycle.pop(0)
    cycle.append(from_comp)

    while cycle:
        to_comp = cycle.pop(0)
        if from_comp.die_id == to_comp.die_id:
            annotate(from_comp, to_comp, idx)
        idx += 1
        from_comp = to_comp

    return ann


def contributing_paths_to_annotations(paths, ann=None):
    """Convert a set of contributing paths (i.e., paths that are part of a deadlock
    loop) into Annotations.
    """
    if ann is None:
        ann = Annotations()
    ann["paths"] = []

    colors = ["red", "blue", "green", "slategray", "cyan", "yellow", "magenta", "darkblue"]

    for i, path in enumerate(paths):
        points = []
        for c in path:
            if c.port >= 0:
                points.append(f"die{c.die_id}.x{c.x}y{c.y}.port{c.port}")
            else:
                points.append(f"die{c.die_id}.x{c.x}y{c.y}")
        ann["paths"].append({"points": points, "color": colors[i % len(colors)]})

    return ann
