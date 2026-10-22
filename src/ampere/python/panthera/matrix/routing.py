# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from typing import List, Optional

from . import components

# This dictionary represents which kind of component can talk to which other kind of
# component over various channels. It's important to get this right according to the
# real protocols or the resulting answer will not be correct.

TARGETS_BY_CHAN_AND_TYPE = {
    "req": {
        "Core": ["PxHA", "*HNF"],
        "PxHA": ["*PmHA"],
        "PmHA": ["MCU"],
        "HNF": ["*MCU"],
        "ADI": ["PmHA"],
        "IOR": ["*PmHA"],
    },
    "rsp0": {
        "Core": ["PxHA"],  # Can cores send responses to other cores?
        "PxHA": ["Core", "*PmHA"],
        "PmHA": ["*PxHA", "MCU"],
        "MCU": ["PmHA", "*HNF"],
    },
    "rsp1": {
        "Core": ["PxHA"],  # Can cores send responses to other cores?
        "PxHA": ["Core", "*PmHA"],
        "PmHA": ["*PxHA", "MCU"],
        "MCU": ["PmHA"],
    },
    "snp": {"PxHA": ["Core"], "PmHA": ["*PxHA"], "HNF": ["*Core"]},
    "dat": {
        "Core": ["Core", "PxHA"],
        "PxHA": ["Core", "*PxHA", "*PmHA"],
        "PmHA": ["*PxHA", "MCU"],
        "HNF": ["*Core"],
        "MCU": ["*Core", "*PmHA", "*HNF"],
    },
}


class Rules:
    def __init__(self):
        self._xy_overrides = set()

    def allowed(self, src, tgt):
        return True

    def disallowed(self, src, tgt):
        return False

    def xy(self, rtr, tgt):
        return ((rtr.die_id, rtr.x, rtr.y), (tgt.die_id, tgt.x, tgt.y, tgt.port)) not in self._xy_overrides

    def add_xy_override(self, rtr, tgt):
        if type(rtr) is not tuple:
            rtr = (rtr.die_id, rtr.x, rtr.y)

        if type(tgt) is not tuple:
            tgt = (tgt.die_id, tgt.x, tgt.y, tgt.port)

        self._xy_overrides.add((rtr, tgt))

    def next_rtr(self, rtr):
        # If you want to force a router to always send to another router, override this
        return None


def get_targets(soc, src, chan, rules=None):
    """Given an SoC source component, return a list of all possible targets
    via the supplied channel.
    """
    target_types = TARGETS_BY_CHAN_AND_TYPE[chan].get(src.dtype, [])
    targets = []
    for target_type in target_types:
        if target_type.startswith("*"):
            cross_die_allowed = True
            target_type = target_type[1:]
        else:
            cross_die_allowed = False
        for c in soc.dev_by_type[target_type]:
            if src.die_id == c.die_id or cross_die_allowed:
                if rules is None or (rules.allowed(src, c) and not rules.disallowed(src, c)):
                    targets.append(c)
    return targets


def path(soc: components.SoC, src: components.Component, tgts: List[components.Component], rules: Optional[Rules]):
    """The main workhorse routine to build a path of components.

    We follow the path from src->tgt. A list of targets, which might have only
    one entry, is supplied. If there's more than one entry, it means that the
    intermediate targets are RDI/ADI links.

    The algorithm is basically divided into the part that does XY routing inside
    a single die, and the extra bit that bridges the RDI link if there are such
    links on the way to the final target.
    """

    def east(loc):
        return soc.rtr_by_xy[(loc.die_id, loc.x + 1, loc.y)]

    def west(loc):
        return soc.rtr_by_xy[(loc.die_id, loc.x - 1, loc.y)]

    def north(loc):
        return soc.rtr_by_xy[(loc.die_id, loc.x, loc.y - 1)]

    def south(loc):
        return soc.rtr_by_xy[(loc.die_id, loc.x, loc.y + 1)]

    path = []

    if not rules:
        rules = Rules()

    while tgts:
        path.append(src)
        a = soc.rtr_by_xy[src.loc()]

        tgt = tgts.pop(0)
        b = soc.rtr_by_xy[tgt.loc()]

        # xy==True  ==> go X first, then Y;
        # xy==False ==> go Y first, then X
        xy = rules.xy(a, tgt)

        assert a.die_id == b.die_id

        while a != b:
            path.append(a)

            next_rtr = rules.next_rtr(a)
            if next_rtr is not None:
                a = next_rtr
                continue

            if xy:
                if a.x < b.x:
                    a = east(a)
                elif a.x > b.x:
                    a = west(a)
                elif a.y < b.y:
                    a = south(a)
                elif a.y > b.y:
                    a = north(a)
            else:
                if a.y < b.y:
                    a = south(a)
                elif a.y > b.y:
                    a = north(a)
                elif a.x < b.x:
                    a = east(a)
                elif a.x > b.x:
                    a = west(a)

        path.append(b)
        path.append(tgt)

        if tgts:
            assert tgt.dtype in {"RDI", "ADI"}
            src = tgt.other

    return path


def paths_to_target(
    soc: components.SoC,
    src: components.Component,
    tgt: components.Component,
    channel: str,
    rules: Optional[Rules] = None,
):
    if not rules:
        rules = Rules()

    paths = []

    if rules.allowed(src, tgt) and not rules.disallowed(src, tgt):
        pass
    else:
        return paths

    if src.die_id != tgt.die_id:
        cross_die_links = soc.rdi_route[(src.die_id, tgt.die_id)]
        if not cross_die_links:
            raise RuntimeError(f"no route from {src} to {tgt}")
        for link in cross_die_links:
            if rules.allowed(src, link[0]) and not rules.disallowed(src, link):
                paths.append(path(soc, src, link + [tgt], rules))
    else:
        paths.append(path(soc, src, [tgt], rules))

    return paths


def paths_to_all_targets(soc: components.SoC, src: components.Component, channel: str, rules: Optional[Rules] = None):
    if not rules:
        rules = Rules()

    paths = []

    for tgt in get_targets(soc, src, channel, rules):
        paths += paths_to_target(soc, src, tgt, channel, rules)

    return paths
