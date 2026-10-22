# Copyright (c) 2024-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

# ---
# jupyter:
#   jupytext:
#     text_representation:
#       extension: .py
#       format_name: percent
#       format_version: '1.3'
#       jupytext_version: 1.17.1
#   kernelspec:
#     display_name: Python 3 (ipykernel)
#     language: python
#     name: python3
# ---

# %% [markdown]
# # Intro

# %% [markdown]
# ## Prologue

# %%
import itertools
import os
import re
import sys
from collections import defaultdict
from contextlib import contextmanager

import matplotlib as mpl
import pandas as pd
import panel as pn

pn.extension(design="material")

# Set PANTHERA_ROOT to a Panthera installation if panthera is not already on
# the Python path.
PANTHERA_ROOT = os.environ.get("PANTHERA_ROOT")
if PANTHERA_ROOT:
    sys.path.append(f"{PANTHERA_ROOT}/lib/python")

# %%
from IPython.display import SVG

import panthera.lynx as lx
import panthera.matrix.domino as dn
from panthera.matrix.DeadlockAndUtilizationModel import DeadlockAndUtilizationModel
from panthera.matrix.domino import *
from panthera.matrix.render import Annotations, SVGRenderer

svg = SVGRenderer()
inferno = mpl.colormaps["inferno"]
accent = mpl.colormaps["Dark2"]

# %% [markdown]
# # Rules

# %% [markdown]
# ## Baseline Rules
#
# Here are the current set of rules to establish the deadlock free baseline (in addition to the topology changes reflected above, of course):
#
# 1. All dies are Y-first routing by default
# 1. Inter-die routing is Y-first except that crossing is allowed in the IOD when transiting hemispheres.
# 1. The PxHAs, PmHAs, and NHPs only use the vertical ADI in their own column.
# 1. When entering and exiting a die vertically, traffic stays in one column.
# 1. DMT and DWT to remote dies is disabled (i.e., the MCU will not respond or send data directly to agents in other CCLs or the IOD)
# 1. Communication through NHP proxies
#     - NHPs no longer communicate directly with MHAs on remote die. Instead, they communicate with other NHPs on those die which then communicate with the MHAs.
#     - Similarly, IORs and ACRs on the IOD communicate remotely to NHPs rather than MHAs directly.


# %%
class Rules(dn.DefaultRules):
    def __init__(self, model):
        self.model = model
        super().__init__(model)

        # self.allow_dmt_to_iod(model)
        self.comtable = self.allowed_communication()

        self.only_horizontal_dmt(model)
        self.phas_and_nhps_and_mhads_use_own_column_for_adi(model)
        self.phas_and_nhps_and_mhads_use_own_column_for_adi(model)
        self.mhas_only_respond_remotely_to_nhps_from_the_iod(model)

    def connect_dies(self, model):
        # Die layout:
        #
        #     1 | 3
        #     2 | 4
        #       0
        #     5 | 7
        #     6 | 8
        #
        ADDITIONAL_MULTI_DIE_PATHS = {(0, 1): [4, 3], (0, 3): [2, 1], (0, 6): [7, 8], (0, 8): [5, 6]}

        super().connect_dies(model)
        return

        have_die = [False] * self.max_dies

        # First, connect everything that the SoC model has direct links for.
        for from_comp, to_comp in model.all_connections():
            have_die[from_comp.die_id] = True
            have_die[to_comp.die_id] = True

        for (from_die, to_die), path in ADDITIONAL_MULTI_DIE_PATHS.items():
            if have_die[from_die] and have_die[to_die]:
                self.connect_die_transitively(from_die, to_die, *path)

    def allowed_communication(self):
        records = []

        for ch in [lx.Channel.values[i] for i in range(len(lx.Channel.values))]:
            for src in [lx.ComponentType.values[i] for i in range(len(lx.ComponentType.values))]:
                for tgt, scope in self.get_targets_by_channel_and_type(ch, src):
                    record = {
                        "channel": str(ch),
                        "src": str(src),
                        "tgt": str(tgt),
                        "scope": "local" if scope == 0 else "remote",
                    }
                    records.append(record)

        return pd.DataFrame.from_records(records).set_index(["channel", "src", "tgt"])

    def phas_and_nhps_and_mhads_use_own_column_for_adi(self, model):
        phas = (
            model.components_by_type(lx.ComponentType.PxHA)
            + model.components_by_type(lx.ComponentType.PmHA)
            + model.components_by_type(lx.ComponentType.NHP)
            + model.components_by_type(lx.ComponentType.MHA_DVM)
        )
        adis = model.components_by_type(lx.ComponentType.ADI)

        for pha in phas:
            for adi in adis:
                if pha.die_id != 0 and pha.die_id == adi.die_id and adi.x != 0 and adi.x != pha.x:
                    self.disallow(pha, adi)

    @staticmethod
    def different_hemispheres(die1, die2):
        return (die1 in {1, 2, 3, 4} and die2 in {5, 6, 7, 8}) or (die2 in {1, 2, 3, 4} and die1 in {5, 6, 7, 8})

    @staticmethod
    def horizontal_neighbors(die1, die2):
        return (die1, die2) in {(1, 3), (3, 1), (2, 4), (4, 2), (5, 7), (7, 5), (6, 8), (8, 6)}

    def only_horizontal_dmt(self, model):
        mcus = (
            model.components_by_type(lx.ComponentType.MCU)
            + model.components_by_type(lx.ComponentType.COM)
            + model.components_by_type(lx.ComponentType.CXL)
        )
        cores = model.components_by_type(lx.ComponentType.Core) + model.components_by_type(lx.ComponentType.PxHA)
        acrs = (
            model.components_by_type(lx.ComponentType.CCL_ACR_CTRACE)
            + model.components_by_type(lx.ComponentType.CCL_ACR_GIC)
            + model.components_by_type(lx.ComponentType.CCL_ACR_MTRACE)
            + model.components_by_type(lx.ComponentType.CCL_ACR_SYS)
        )

        for mcu in mcus:
            for core in cores + acrs:
                if mcu.die_id != core.die_id and not self.horizontal_neighbors(mcu.die_id, core.die_id):
                    self.disallow(lx.Channel.DAT, mcu, core)
                else:
                    self.dmt_allow(core.die_id, mcu.die_id)

    def mhas_only_respond_remotely_to_nhps_from_the_iod(self, model):
        mhas = model.components_by_type(lx.ComponentType.MHA_SYS) + model.components_by_type(lx.ComponentType.MHA_GIC)
        mhas = model.components_by_type(lx.ComponentType.MHA_SYS) + model.components_by_type(lx.ComponentType.MHA_GIC)
        nhps = model.components_by_type(lx.ComponentType.NHP)

        for mha in mhas:
            for nhp in nhps:
                if mha.die_id != 0 and mha.die_id != nhp.die_id:
                    self.disallow(lx.Channel.RSP0, mha, nhp)
                    self.disallow(lx.Channel.RSP1, mha, nhp)
                    self.disallow(lx.Channel.DAT, mha, nhp)

    def allow_dmt_to_iod(self, model):
        tgts = self.get_targets_by_channel_and_type(lx.Channel.DAT, lx.ComponentType.MCU)
        tgts += [(lx.ComponentType.IORr, lx.Scope.CrossDie)]
        self.set_targets_by_channel_and_type(lx.Channel.DAT, lx.ComponentType.MCU, tgts)


# %% [markdown]
# ## Utilization Rules
#
# Routing rules further refined to optimize link utilization.


# %%
class UtilRules(Rules):
    def __init__(self, model):
        super().__init__(model)
        self.set_channel_map(lx.Opcode.SnpResp, lx.Channel.RSP0)
        self.set_channel_map(lx.Opcode.ReadReceipt, lx.Channel.RSP0)
        self.iod_target_based_routing(model)
        self.pmha_mcu_assignment(model)
        self.pxha_to_pmha_restrictions(model)
        self.set_xy_overrides_in_iod(model)
        self.add_xy_overrides_for_io_bw(model)

    def pmha_mcu_assignment(self, model):
        """Not all PmHAs talk to all MCUs. P[xm]HAs in the top half talk to the MCUs in the top
        half, and those in the bottom half talk to the MCUs in the bottom half."""
        phas = model.components_by_type(lx.ComponentType.PmHA) + model.components_by_type(lx.ComponentType.PxHA)
        mcus = model.components_by_type(lx.ComponentType.MCU)

        for pha in phas:
            for mcu in mcus:
                if pha.die_id == mcu.die_id:
                    if (pha.y in (0, 1) and mcu.y not in (0, 1)) or (pha.y in (2, 3) and mcu.y not in (2, 3)):
                        self.disallow(pha, mcu)

    def pxha_to_pmha_restrictions(self, model):
        """PxHAs really only talk to the PmHA at their same location.

        This is true even across CCLs: a PxHA at router location (1,0) will talk to its
        neighboring PmHA on the same router, but it will also only talk to PmHAs at (1,0)
        on other dies."""
        pxhas = model.components_by_type(lx.ComponentType.PxHA)
        pmhas = model.components_by_type(lx.ComponentType.PmHA)

        for pxha in pxhas:
            for pmha in pmhas:
                if (pxha.x, pxha.y) != (pmha.x, pmha.y):
                    self.disallow(pxha, pmha)

    def create_cross_die_routes(self, model, srcs, get_waypoint, targets=None, channels=None, filter=None):
        """Add all possible cross-die routes between the sources and targets."""
        if channels is None:
            channels = self.all_channels()

        for channel in channels:
            for src in srcs:
                for tgt in targets if targets else model.all_targets(self, src, channel):
                    if tgt.die_id != src.die_id and (filter is None or filter(src, tgt, channel)):
                        for adis in self._each_cross_die_route(src, tgt, channel, get_waypoint):
                            # if src.type == lx.ComponentType.IORr and tgt.type == lx.ComponentType.PmHA and src.x == 5 and channel == lx.Channel.REQ:
                            #    print(f'ADDING CROSS DIE ROUTE for {channel}: {src} -> {tgt}: {adis}')
                            self.add_cross_die_route_src_tgt(channel, src, tgt, adis)

    def _each_cross_die_route(self, src, tgt, channel, get_waypoint, adis=[]):
        """Yield cross-die routes of ADI links between `src` and `tgt`. This is done by adding
           waypoints one at a time with the supplied `get_waypoint()` function. Arguments:

               src          - The current source agent. This moves as we recurse---it starts with
                              the original source and then takes on the value of the "far" side of
                              each ADI link pass through on the way.
               tgt          - The ultimate target agent. We don't really need it ourselves, except
                              for error messages. But we do need to pass it to the waypoint
                              generator to get the next waypoint.
               channel      - The CHI channel we're tracking. Also not used locally, just passed
                              to the waypoint generator.
               get_waypoint - Caller supplied function to generate the next waypoint(s) for this
                              route.
               adis         - Incoming list of ADI links. We will add to this and eventually yield
                              a full list from src -> tgt (tgt die, technically, as we are not
                              concerned with die-local routing here).

        We're done when `get_waypoint()` gives us back a null link, represented as (None, None).
        """
        for a, b in get_waypoint(src, tgt, channel):
            if a is None:
                if adis:
                    yield adis
            else:
                assert b is not None
                if len(adis) > 5:
                    raise RuntimeError(f"too many adis from {src} -> {tgt}: {adis + [a]}")
                yield from self._each_cross_die_route(b, tgt, channel, get_waypoint, adis + [a])

    PMHA_COORD_TO_AGENT_ID = {
        (1, 0, 1): 0x60,
        (3, 0, 1): 0x63,
        (1, 1, 1): 0x64,
        (2, 1, 0): 0x65,
        (2, 1, 1): 0x66,
        (3, 1, 1): 0x67,
        (1, 2, 1): 0x68,
        (2, 2, 0): 0x69,
        (2, 2, 1): 0x6A,
        (3, 2, 1): 0x6B,
        (1, 3, 1): 0x6C,
        (3, 3, 1): 0x6F,
    }

    RC_NAMES = [
        ["RC7", "RC7", "RC0", "RC0"],
        ["RC7", "RC7", "RC0", "RC0"],
        ["RC6", "RC6", "RC1", "RC1"],
        ["RC6", "RC6", "RC1", "RC1"],
        ["RC5", "RC5", "RC2", "RC2"],
        ["RC5", "RC5", "RC2", "RC2"],
        ["RC4", "RC4", "RC3", "RC3"],
        ["RC4", "RC4", "RC3", "RC3"],
    ]

    def iod_target_based_routing(self, model):
        """From the IOD, PmHA target routes are determined based on target ids bits (TgtID[2:1]). These bits
        tell us which of 4 ADI connections to use. We don't have TgtIDs on agents in this model, but we can
        map from X,Y,port to those ids.
        """
        adi_index = {}
        adi_xp_index = {}

        for a, b in model.all_connections():
            adi_xp_index[(a.die_id, b.die_id, a.x, a.port)] = (a, b)

            if a.die_id == 0:
                if (a.x, a.y) in {(3, 2), (4, 2), (3, 1), (4, 1)}:
                    adi_index[(a.die_id, b.die_id, 0)] = (a, b)
                elif (a.x, a.y) in {(2, 0), (2, 3), (5, 3), (5, 0)}:
                    adi_index[(a.die_id, b.die_id, 1)] = (a, b)
                elif (a.x, a.y) in {(1, 2), (1, 1), (6, 2), (6, 1)}:
                    adi_index[(a.die_id, b.die_id, 2)] = (a, b)
                elif (a.x, a.y) in {(0, 3), (0, 0), (7, 3), (7, 0)}:
                    adi_index[(a.die_id, b.die_id, 3)] = (a, b)
            else:
                if a.x == 1:
                    adi_index[(a.die_id, b.die_id, 0)] = (a, b)
                elif a.x == 3:
                    adi_index[(a.die_id, b.die_id, 3)] = (a, b)
                elif a.port == 0:
                    adi_index[(a.die_id, b.die_id, 1)] = (a, b)
                elif a.port == 2:
                    adi_index[(a.die_id, b.die_id, 2)] = (a, b)
                elif a.x == 0:
                    adi_index[(a.die_id, b.die_id, a.y)] = (a, b)
                else:
                    raise RuntimeError(f"unexpected ADI location {a.die_id}: x{a.x}y{a.y}p{a.port}")

        pmha_index = {}

        for pmha in model.components_by_type(lx.ComponentType.PmHA):
            pmha_index[pmha] = (self.PMHA_COORD_TO_AGENT_ID[(pmha.x, pmha.y, pmha.port)] >> 1) & 0b11

        iod_types = [
            lx.ComponentType.IORr,
            lx.ComponentType.IORw,
            lx.ComponentType.CXL,
            lx.ComponentType.IOD_ACR_SYS,
            lx.ComponentType.IOD_ACR_GIC,
            lx.ComponentType.IOD_ACR_MTRACE,
            lx.ComponentType.IOD_ACR_SMMU,
        ]
        # Why were MHAs in here? [lx.ComponentType.MHA_SYS, lx.ComponentType.MHA_GIC, lx.ComponentType.MHA_DVM]
        iod_comps = []

        for iod_type in iod_types:
            iod_comps += [c for c in model.components_by_type(iod_type) if c.die_id == 0]

        def get_waypoint(src, tgt, channel, check=False):
            if src.die_id == tgt.die_id:
                yield (None, None)
                return

            from_die = src.die_id
            to_die = self.MULTI_DIE_PATHS.get((from_die, tgt.die_id), [tgt.die_id])[0]

            if check:
                print(f"get_waypoint: {src} -> {tgt}; from_die={from_die} to_die={to_die}")

            if from_die == 0:
                # We're coming from the IOD. Pick a column based on the pre-computed index.
                yield adi_index[(from_die, to_die, pmha_index[tgt])]
            else:
                # For any other die, just stay in the column we're in
                yield adi_xp_index[(from_die, to_die, src.x, src.port)]

        def get_waypoint_alt(src, tgt, channel, check=False):
            """Try a selective Y-first die routing from the IOD. The idea is to force the IORs to
            use the vertical pathways, since there is more BW in that direction. So if an IOR is
            on the left half of the IOD, it will travel vertically through the CCLs on the left
            side even if ultimately targeting a die on the right. And vice-versa.
            """
            if src.die_id == tgt.die_id:
                yield (None, None)
                return

            from_die = src.die_id

            if from_die == 0:
                if src.x < 4:  # go left
                    if tgt.die_id < 5:  # go up
                        to_die = 2
                    else:  # go down
                        to_die = 5
                else:  # go right
                    if tgt.die_id < 5:  # go up
                        to_die = 4
                    else:  # go down
                        to_die = 7
                yield adi_index[(from_die, to_die, pmha_index[tgt])]
            else:
                to_die = self.MULTI_DIE_PATHS.get((from_die, tgt.die_id), [tgt.die_id])[0]
                # For any other die, just stay in the column we're in
                if (from_die, to_die) in ((1, 3), (3, 1), (2, 4), (4, 2), (5, 7), (7, 5), (6, 8), (8, 6)):
                    for y in range(4):
                        yield adi_index[(from_die, to_die, y)]
                else:
                    yield adi_xp_index[(from_die, to_die, src.x, src.port)]

        def get_waypoint_alt2(src, tgt, channel, check=False):
            """Similar to the first alternative, except that the central IORs can use either
            left or right interfaces depending on their destination dies."""
            if src.die_id == tgt.die_id:
                yield (None, None)
                return

            from_die = src.die_id

            if from_die == 0:
                if src.x < 2:  # go left
                    to_die = 2 if tgt.die_id < 5 else 5
                elif src.x > 5:  # go right
                    to_die = 4 if tgt.die_id < 5 else 7
                else:  # go left or right depending on target die
                    if tgt.die_id in {1, 2}:
                        to_die = 2
                    elif tgt.die_id in {3, 4}:
                        to_die = 4
                    elif tgt.die_id in {5, 6}:
                        to_die = 5
                    elif tgt.die_id in {7, 8}:
                        to_die = 7
                yield adi_index[(from_die, to_die, pmha_index[tgt])]
            else:
                to_die = self.MULTI_DIE_PATHS.get((from_die, tgt.die_id), [tgt.die_id])[0]
                # For any other die, just stay in the column we're in
                if (from_die, to_die) in ((1, 3), (3, 1), (2, 4), (4, 2), (5, 7), (7, 5), (6, 8), (8, 6)):
                    for y in range(4):
                        yield adi_index[(from_die, to_die, y)]
                else:
                    yield adi_xp_index[(from_die, to_die, src.x, src.port)]

        def get_waypoint_alt3(src, tgt, channel, check=False):
            """Yet more swizzling."""
            if src.die_id == tgt.die_id:
                yield (None, None)
                return

            from_die = src.die_id

            rc_to_die_route = {
                #         1         3
                #         2         4
                #     RC0  RC1  RC2  RC3
                #     RC7  RC6  RC5  RC4
                #         5         7
                #         6         8
                #
                # tgt die:    1  2  3  4  5  6  7  8
                "RC0": [None, 2, 2, 2, 2, 5, 5, 5, 5],
                "RC1": [None, 2, 2, 4, 4, 5, 5, 5, 5],
                "RC2": [None, 2, 2, 4, 4, 7, 7, 7, 7],
                "RC3": [None, 4, 4, 4, 4, 7, 7, 7, 7],
                "RC4": [None, 4, 4, 4, 4, 7, 7, 7, 7],
                "RC5": [None, 4, 4, 4, 4, 5, 5, 7, 7],
                "RC6": [None, 2, 2, 2, 2, 5, 5, 7, 7],
                "RC7": [None, 2, 2, 2, 2, 5, 5, 5, 5],
            }

            if from_die == 0:
                rc = self.RC_NAMES[src.x][src.y]
                to_die = rc_to_die_route[rc][tgt.die_id]
                yield adi_index[(from_die, to_die, pmha_index[tgt])]
            else:
                to_die = self.MULTI_DIE_PATHS.get((from_die, tgt.die_id), [tgt.die_id])[0]
                # For any other die, just stay in the column we're in
                if (from_die, to_die) in ((1, 3), (3, 1), (2, 4), (4, 2), (5, 7), (7, 5), (6, 8), (8, 6)):
                    for y in range(4):
                        yield adi_index[(from_die, to_die, y)]
                else:
                    yield adi_xp_index[(from_die, to_die, src.x, src.port)]

        self.create_cross_die_routes(model, iod_comps, get_waypoint, targets=pmha_index.keys())

    @staticmethod
    def all_channels():
        return (lx.Channel.REQ, lx.Channel.RSP0, lx.Channel.RSP1, lx.Channel.SNP, lx.Channel.DAT)

    IOD_XY_OVERRIDES = {
        # Octant 1 fixes
        ((2, 3), (1, 2)): ((3, 0, 0),),
        ((1, 2),): ((3, 3, 0),),
        # Octant 2 fixes
        ((5, 3), (6, 2)): ((4, 0, 0),),
        ((6, 2),): ((4, 3, 0),),
        # Octant 3 fixes
        ((1, 1),): ((3, 0, 0),),
        ((1, 1), (2, 0)): ((3, 3, 0),),
        # Octant 4 fixes
        ((6, 1),): ((4, 0, 0),),
        ((6, 1), (5, 0)): ((4, 3, 0),),
    }

    def set_xy_overrides_in_iod(self, model):
        # self.west_last(model)

        interesting_target_types = [
            lx.ComponentType.ADI,
            lx.ComponentType.IORr,
            lx.ComponentType.IORw,
            lx.ComponentType.IOHr,
            lx.ComponentType.IOHw,
        ]
        targets = [
            c for c in sum([model.components_by_type(ct) for ct in interesting_target_types], []) if c.die_id == 0
        ]

        xyp_set = set()
        for tgt in targets:
            assert (tgt.x, tgt.y, tgt.port) not in xyp_set
            xyp_set.add((tgt.x, tgt.y, tgt.port))

        tgts_by_xyp = {(c.x, c.y, c.port): c for c in targets}

        if len(tgts_by_xyp):
            for rtrs, tgts in self.IOD_XY_OVERRIDES.items():
                for rtr_x, rtr_y in rtrs:
                    rtr = model.find_rtr(lx.Location(0, rtr_x, rtr_y))
                    for tgt_x, tgt_y, tgt_p in tgts:
                        if type(tgt_x) is lx.ComponentType:
                            tgt_type = tgt_x
                            tgt_x = tgt_y
                            tgt_y = tgt_p
                            tgt = [c for c in model.components_by_type(tgt_type) if c.x == tgt_x and c.y == tgt_y][0]
                            self.add_xy_override(rtr, tgt)
                        else:
                            tgt = tgts_by_xyp[(tgt_x, tgt_y, tgt_p)]
                            self.add_xy_override(rtr, tgt)

    def add_xy_overrides_for_io_bw(self, model):
        """Override the routing to be X first as ADI traffic enters a CCL to improve
        utilization of the vertical columns."""
        pmhas = model.components_by_type(lx.ComponentType.PmHA)
        for pmha in pmhas:
            if pmha.die_id in (1, 2, 3, 4, 5, 6, 7, 8):  # (2, 4, 5, 7):  # try skipping the outer dies
                if pmha.die_id in (1, 2, 7, 8):
                    rtr_y = 3
                else:
                    rtr_y = 0
                rtr = model.find_rtr(lx.Location(pmha.die_id, 2, rtr_y))
                if pmha.x < 2:  # was != 2, but let's try only a subset
                    self.add_xy_override(rtr, pmha)


# %% [markdown]
# # Configurations

# %%
domino_deadlock = DeadlockAndUtilizationModel(Domino, Rules)
domino = DeadlockAndUtilizationModel(Domino, UtilRules)
domino_octant = DeadlockAndUtilizationModel(Domino_Octant, UtilRules)
domino_hemi = DeadlockAndUtilizationModel(Domino_Hemi, UtilRules)
domino_octant1_io = DeadlockAndUtilizationModel(Domino_Octant1_IO, UtilRules)
domino_octant2_io = DeadlockAndUtilizationModel(Domino_Octant2_IO, UtilRules)
domino_octant3_io = DeadlockAndUtilizationModel(Domino_Octant3_IO, UtilRules)
domino_octant4_io = DeadlockAndUtilizationModel(Domino_Octant4_IO, UtilRules)
domino_iod_only = DeadlockAndUtilizationModel(Domino_IOD_Only, UtilRules)


# %% [markdown]
# # Validation


# %%
# %%
class TooManyErrors(RuntimeError):
    def __init__(self, errors):
        self.errors = errors
        self.errors.error_messages.append("maximum error count reached; stopping")


class RunCheck:
    def __init__(self, max_errors):
        self.max_errors = max_errors
        self.errors = 0
        self.error_messages = []

    def log(self, msg):
        self.error_messages.append(msg)
        self.errors += 1
        if self.errors > self.max_errors:
            raise TooManyErrors(self)

    def report(self):
        print("\n".join(self.error_messages))


@contextmanager
def run_and_check(max_errors=10):
    try:
        yield RunCheck(max_errors)
    except TooManyErrors as exc:
        exc.errors.report()


# %%
# %%
def show_allowed_communication(table):
    row = pn.Row()

    for chan in ("REQ", "RSP0", "RSP1", "SNP", "DAT"):
        row.append(pn.Column(chan, pn.pane.HTML(table.loc[chan].to_html())))

    return row


# %%
TD_COLOR = {"L": "#4caf50", "R": "#ba68c8", "X": "#f5f5f5"}


def show_comtable_square(table, chan):
    all_srcs = sorted(set(table.reset_index()["src"]))
    all_tgts = sorted(set(table.reset_index()["tgt"]))
    table.sort_index(inplace=True)
    html = f"<table>\n<tr><th>{chan}</th>"
    for tgt in all_tgts:
        if tgt not in {"IOR", "IOH"}:
            html += f"<th>{tgt}</th>"
    html += "</tr>\n"
    for src in all_srcs:
        if src in {"IOR", "IOH"}:
            continue
        cols = ""
        for tgt in all_tgts:
            if tgt in {"IOR", "IOH"}:
                continue
            try:
                allowed = "R" if table.loc[(chan, src, tgt)]["scope"] == "remote" else "L"
            except KeyError:
                allowed = "X"

            cols += f'<td style="background-color: {TD_COLOR[allowed]}">{allowed}</td>'
        html += f"<tr><th>{src}</th>{cols}</tr>\n"
    html += "</table>\n"
    return html


# %%
tables = [
    "<p>\n" + show_comtable_square(domino.rules.comtable, chan) + "</p>" for chan in ("REQ", "RSP0", "SNP", "DAT")
]

# %%
pn.pane.HTML(
    "\n".join(tables),
    stylesheets=[
        """
table, th, td {
  border: 1px solid black;
  border-collapse: collapse;
}
td, th {
  padding-left: 10px;
  padding-right: 10px;
  text-align: center;
}
"""
    ],
)


# %%
def check_path_for_channel(soc, src, tgt, ch, errors):
    """Verify that we can reach from src -> tgt on the given channel."""
    try:
        paths = lx.paths_to_target(soc.model, src, tgt, ch, soc.rules)
    except:
        paths = []
    try:
        paths = lx.paths_to_target(soc.model, src, tgt, ch, soc.rules)
    except:
        paths = []
    if len(paths) == 0:
        errors.log(f"No path from {src} -> {tgt} on {ch} channel")


# %%
# %%
def check_communication_helper(soc, chan, from_type, to_type, errors, valid_path=None):
    srcs = soc.model.components_by_type(from_type)
    tgts = soc.model.components_by_type(to_type)

    for src in srcs:
        for tgt in tgts:
            if valid_path is None or valid_path(src, tgt):
                check_path_for_channel(soc, src, tgt, chan, errors)


def check_communication(soc, verbose=False):
    def die_match(src, tgt):
        return src.die_id == tgt.die_id

    def location_match(src, tgt):
        return (src.x, src.y) == (tgt.x, tgt.y)

    def same_die_or_horizontal_neighbor(src, tgt):
        return src.die_id == tgt.die_id or (
            (src.die_id, tgt.die_id) in {(1, 3), (3, 1), (2, 4), (4, 2), (5, 7), (7, 5), (6, 8), (8, 6)}
        )

    def mha_on_iod(src, tgt):
        return src.die_id == 0

    def match_same_half_ccl(src, tgt):
        if src.die_id == tgt.die_id:
            for half in ((0, 1), (2, 3)):
                if src.y in half and tgt.y in half:
                    return True
        return False

    with run_and_check(max_errors=10) as errors:
        for (chan_name, from_type_name, to_type_name), data in soc.rules.comtable.iterrows():
            chan = lx.Channel.names[chan_name]
            from_type = lx.ComponentType.names[from_type_name]
            to_type = lx.ComponentType.names[to_type_name]
            scope = data["scope"]
            if verbose:
                print(f"checking {chan} {from_type} -> {to_type} for {scope} die")
            if from_type in {lx.ComponentType.PxHA, lx.ComponentType.PmHA} and to_type in {
                lx.ComponentType.PxHA,
                lx.ComponentType.PmHA,
            }:
                check_communication_helper(soc, chan, from_type, to_type, errors, location_match)
            elif (
                from_type in {lx.ComponentType.MCU, lx.ComponentType.COM, lx.ComponentType.CXL}
                and to_type
                in {
                    lx.ComponentType.Core,
                    lx.ComponentType.PxHA,
                    lx.ComponentType.CCL_ACR_GIC,
                    lx.ComponentType.CCL_ACR_SYS,
                }
                and chan == lx.Channel.DAT
            ):
                check_communication_helper(soc, chan, from_type, to_type, errors, same_die_or_horizontal_neighbor)
            elif (
                from_type in {lx.ComponentType.MHA_SYS, lx.ComponentType.MHA_GIC}
                and to_type == lx.ComponentType.NHP
                and chan in {lx.Channel.RSP0, lx.Channel.RSP1, lx.Channel.DAT}
            ):
                check_communication_helper(soc, chan, from_type, to_type, errors, mha_on_iod)
            elif from_type in {lx.ComponentType.PmHA, lx.ComponentType.PxHA} and to_type == lx.ComponentType.MCU:
                check_communication_helper(soc, chan, from_type, to_type, errors, match_same_half_ccl)
            elif scope == "local":
                check_communication_helper(soc, chan, from_type, to_type, errors, die_match)
            else:
                check_communication_helper(soc, chan, from_type, to_type, errors)

        if errors.errors > 0:
            print("failed")
        else:
            print("passed")

        return errors.errors == 0


# %%
check_communication(domino, verbose=False)

# %%
ann = Annotations()
for ty in lx.ComponentType.values.values():
    for comp in domino.model.components_by_type(ty):
        die_id = comp.die_id
        x = comp.x
        y = comp.y
        port = comp.port
        didx = comp.didx
        if port != -1 and hasattr(comp, "DEVICE_TYPE"):
            if didx != -1:
                ann[f"die{die_id}"][f"x{x}y{y}"][f"port{port}"][f"dev{didx}"]["label"] = comp.DEVICE_TYPE
            else:
                ann[f"die{die_id}"][f"x{x}y{y}"][f"port{port}"]["label"] = comp.DEVICE_TYPE

# %%
# Uncomment this line to see an SoC diagram annotated with component types
# pn.pane.HTML(svg(domino.draw_soc, annotations=ann, scale=0.6))

# %% [markdown]
# # Deadlock Check

# %%
domino_deadlock.check_deadlocks()

# %%
domino.check_deadlocks()

# %%
# domino_deadlock.show_routes(lx.Channel.DAT, domino_deadlock.model.components_by_type(lx.ComponentType.MCU), domino_deadlock.model.components_by_type(lx.ComponentType.IORr))

# %%
# If the deadlock check shows deadlocks present on some channel, you can see a table of
# the contriubing paths by uncommenting the line below (and changing the channel to
# whichever one you need to view). Sometimes this is easier to read than the diagram.

# domino.deadlock_contributing_paths[lx.Channel.SNP]

# %%
# To see a drawing of the whole SoC wtih deadlocking paths highlighted, uncomment the
# line below. If there are no deadlocks, this will just show an SoC diagram with no
# highlighted paths.
domino.deadlock_view()


# %% [markdown]
# # Utilization


# %%
def busiest_link(acc, chan):
    max_traffic = 0.0

    for (from_comp, to_comp), per_opcode_traffic in acc.as_dict(chan).items():
        link_traffic = sum(per_opcode_traffic.values())
        if link_traffic > max_traffic:
            max_traffic = link_traffic

    return max_traffic


def busiest_links(acc):
    links = {}
    for chan in (lx.Channel.REQ, lx.Channel.RSP0, lx.Channel.RSP1, lx.Channel.SNP, lx.Channel.DAT):
        links[str(chan)] = busiest_link(acc, chan)
    return links


def dram_read_bw(acc, mcu_type=lx.ComponentType.MCU):
    bw = 0.0
    for (from_comp, to_comp), per_opcode_traffic in acc.as_dict(lx.Channel.DAT).items():
        if from_comp.type == mcu_type:
            link_traffic = sum(
                [
                    traffic
                    for opcode, traffic in per_opcode_traffic.items()
                    if opcode in {lx.Opcode.CompData, lx.Opcode.DataSepResp}
                ]
            )
            link_bw = link_traffic / 100.0 * 64.0 * 2.0
            bw += link_bw
    return bw


def dram_write_bw(acc, mcu_type=lx.ComponentType.MCU):
    bw = 0.0
    for (from_comp, to_comp), per_opcode_traffic in acc.as_dict(lx.Channel.DAT).items():
        if to_comp.type == mcu_type:
            link_traffic = sum(
                [
                    traffic
                    for opcode, traffic in per_opcode_traffic.items()
                    if opcode in {lx.Opcode.CopyBackWrData, lx.Opcode.NonCopyBackWrData}
                ]
            )
            link_bw = link_traffic / 100.0 * 64.0 * 2.0
            bw += link_bw
    return bw


def l3_read_bw(acc):
    bw = 0.0
    for (from_comp, to_comp), per_opcode_traffic in acc.as_dict(lx.Channel.DAT).items():
        if from_comp.type == lx.ComponentType.Core:
            link_traffic = sum(
                [traffic for opcode, traffic in per_opcode_traffic.items() if opcode == lx.Opcode.CompData]
            )
            link_bw = link_traffic / 100.0 * 64.0 * 2.0
            bw += link_bw
    return bw


def l3_write_bw(acc):
    bw = 0.0
    for (from_comp, to_comp), per_opcode_traffic in acc.as_dict(lx.Channel.DAT).items():
        if to_comp.type == lx.ComponentType.Core:
            link_traffic = sum(
                [traffic for opcode, traffic in per_opcode_traffic.items() if opcode == lx.Opcode.CopyBackWrData]
            )
            link_bw = link_traffic / 100.0 * 64.0 * 2.0
            bw += link_bw
    return bw


def find_max_util(soc, scenario):
    scale = 1.0
    tries = 1

    while True:
        scaled_scenario = {k: v * scale for k, v in scenario.items()}
        acc = soc.calc_utilization(**scaled_scenario)
        links = busiest_links(acc)
        max_busiest_link = max(links.values())
        err = 100.0 - max_busiest_link
        # print(f'relax: {tries} scale={scale:.2f}, busiest link={max_busiest_link:.2f}, err={err:.3f}')

        if abs(err) < 1.0 or tries == 10:
            links.update(
                {
                    "DRAM RD BW": dram_read_bw(acc),
                    "DRAM WR BW": dram_write_bw(acc),
                    "L3 RD BW": l3_read_bw(acc),
                    "L3 WR BW": l3_write_bw(acc),
                }
            )
            return links, scaled_scenario

        scale += err / 100.0
        tries += 1


# %% [markdown]
# ## DDR Scenarios

# %%
read = {"ReadShared": 7, "WriteEvictFull": 7, "Evict": 7}
write = {"MakeUnique": 7, "WriteBackFull": 7, "WriteBackFull L3": 7}

copy = read.copy()
copy.update(write)
copy = {k: v / 2 for k, v in copy.items()}

read_l3 = {"ReadShared L3 Hit": 7, "WriteEvictFull": 7}

read_50pct_l3 = {"ReadShared": 4, "ReadShared L3 Hit": 4, "WriteEvictFull": 8, "Evict": 4}

# %%
scenarios = (
    ("read DRAM", read),
    ("write DRAM", write),
    ("copy DRAM", copy),
    ("read L3", read_l3),
    ("read L3 50% Hit", read_50pct_l3),
)


# %%
def compute_bw_for_scenario(soc, scenario, config):
    scenario_name, scenario_spec = scenario
    record, _ = find_max_util(soc, scenario_spec)
    record["scenario"] = scenario_name
    record["config"] = config
    return record


def compute_bw_for_scenarios(socs, scenarios):
    records = []
    for config_name, soc in socs:
        for scenario in scenarios:
            print(f"Running {scenario[0]} for {config_name}...", end="")
            record = compute_bw_for_scenario(soc, scenario, config_name)
            records.append(record)
            print("")
    return pd.DataFrame.from_records(records)


def show_bw(socs, scenarios):
    df = compute_bw_for_scenarios(socs, scenarios)
    return pn.pane.HTML(df.sort_values("scenario").set_index(["scenario", "config"]).to_html())


# %%
show_bw(
    [
        ("Domino", domino_octant),
    ],
    scenarios,
)

# %%
show_bw(
    [
        ("Domino", domino_hemi),
    ],
    scenarios,
)

# %%
# domino_hemi.view(**find_max_util(domino_hemi, read)[1])

# %%
# read_bw, read_out = find_max_util(domino_hemi, read)

# %%
# domino_hemi.view(**read_out)

# %% [markdown]
# ## CXL Scenarios

# %%
read_cxl = {"ReadShared CXL": 2, "WriteEvictFull": 2, "Evict": 2}

# %%
cxl_scenarios = (("read CXL", read_cxl),)


# %%
def compute_bw_for_cxl_scenario(soc, cxls, scenario):
    scale = 1.0
    tries = 1

    while True:
        scaled_scenario = {k: v * scale for k, v in scenario.items()}
        acc = lx.Accumulator()
        for cxl in cxls:
            for source in soc.sources:
                soc.add_cxl_scenarios(source, cxl, acc, **scaled_scenario)
        links = busiest_links(acc)
        max_busiest_link = max(links.values())
        err = 100.0 - max_busiest_link
        # print(f'relax: {tries} scale={scale:.2f}, busiest link={max_busiest_link:.2f}, err={err:.3f}')

        if abs(err) < 1.0 or tries == 10:
            links.update(
                {
                    "CXL RD BW": dram_read_bw(acc, mcu_type=lx.ComponentType.CXL),
                    "CXL WR BW": dram_write_bw(acc, mcu_type=lx.ComponentType.CXL),
                }
            )
            return links, acc

        scale += err / 100.0
        tries += 1


def compute_bw_across_cxl_locations_for_scenario(soc, scenario, records):
    scenario_name, scenario_spec = scenario
    cxls = soc.model.components_by_type(lx.ComponentType.CXL)
    for cxl in cxls:
        record, _ = compute_bw_for_cxl_scenario(soc, [cxl], scenario_spec)
        record["scenario"] = scenario_name
        record["CXL"] = f"x{cxl.x}y{cxl.y}"
        records.append(record)


def compute_bw_across_pairs_of_cxl_locations_for_scenario(soc, scenario, records):
    scenario_name, scenario_spec = scenario
    cxls = soc.model.components_by_type(lx.ComponentType.CXL)
    for cxl1, cxl2 in itertools.combinations(cxls, 2):
        if cxl1 != cxl2:
            record, _ = compute_bw_for_cxl_scenario(soc, [cxl1, cxl2], scenario_spec)
            record["scenario"] = scenario_name
            record["CXL"] = f"x{cxl1.x}y{cxl1.y} x{cxl2.x}y{cxl2.y}"
            if record["CXL RD BW"] + 2 < 256.0:
                records.append(record)


def compute_bw_across_cxl_locations_for_scenarios(soc, scenarios):
    records = []
    for scenario in scenarios:
        compute_bw_across_pairs_of_cxl_locations_for_scenario(soc, scenario, records)
    return pd.DataFrame.from_records(records)


def cxl_at(soc, loc):
    m = re.match("x([0-9]+)y([0-9]+)", loc)
    assert m
    x = int(m.group(1))
    y = int(m.group(2))
    return [cxl for cxl in soc.model.components_by_type(lx.ComponentType.CXL) if cxl.x == x and cxl.y == y][0]


def cxl_pair(soc, loc1, loc2):
    return [cxl_at(soc, loc1), cxl_at(soc, loc2)]


def show_cxl_bw(soc, scenarios):
    df = compute_bw_across_cxl_locations_for_scenarios(soc, scenarios)
    if len(df) == 0:
        return pn.pane.HTML("<p><b>All scenarious achieved target bandwidth</b>.</p>")
    else:
        return pn.pane.HTML(df.sort_values("scenario").set_index(["scenario", "CXL"]).to_html())


# %% [markdown]
# ### Hemisphere

# %%
# show_cxl_bw(domino_hemi, cxl_scenarios)

# %% [markdown]
# ### Octant 1

# %%
# show_cxl_bw(domino_octant1_io, cxl_scenarios)

# %%
# _, cxl_acc = compute_bw_for_cxl_scenario(domino_8x4_octant_io, cxl_pair(domino_8x4_octant_io, 'x3y0', 'x4y0'), read_cxl)
# domino_8x4_octant_io.view2(cxl_acc)

# %% [markdown]
# ### Octant 2

# %%
# show_cxl_bw(domino_octant2_io, cxl_scenarios)

# %% [markdown]
# ### Octant 3

# %%
# show_cxl_bw(domino_octant3_io, cxl_scenarios)

# %% [markdown]
# ### Octant 4

# %%
# show_cxl_bw(domino_octant4_io, cxl_scenarios)

# %% [markdown]
# ## IO Scenarios

# %%
IOR_NAMES = [
    ["IOR7", "IOR7", "IOR0", "IOR0"],
    ["IOR7", "IOR7", "IOR0", "IOR0"],
    ["IOR6", "IOR6", "IOR1", "IOR1"],
    ["IOR6", "IOR6", "IOR1", "IOR1"],
    ["IOR5", "IOR5", "IOR2", "IOR2"],
    ["IOR5", "IOR5", "IOR2", "IOR2"],
    ["IOR4", "IOR4", "IOR3", "IOR3"],
    ["IOR4", "IOR4", "IOR3", "IOR3"],
]


def compute_bw_for_io_pair(soc, io1, io2, scenario1, scenario2):
    scale = 1.0
    tries = 1

    min_scale = 0.1
    max_scale = 2.0

    while True:
        scaled_scenario1 = {k: v * scale for k, v in scenario1.items()}
        scaled_scenario2 = {k: v * scale for k, v in scenario2.items()}

        acc = soc.add_scenarios(io1, **scaled_scenario1)
        soc.add_scenarios(io2, acc=acc, **scaled_scenario2)

        links = busiest_links(acc)
        max_busiest_link = max(links.values())
        err = 100.0 - max_busiest_link

        # print(f'relax: {tries} scale={scale:.2f}, busiest link={max_busiest_link:.2f}, err={err:.3f}')

        if abs(err) < 1.0 or tries == 100:
            links.update({"DRAM RD BW": dram_read_bw(acc), "DRAM WR BW": dram_write_bw(acc)})
            if tries == 10:
                print(f"failed to converge for IO1 = {io1}, IO2 = {io2}")
            return links, acc
        elif err < 0.0:
            # We were too busy this time around. We never need to go as high as (and certainly not
            # higher) than this current scale again
            max_scale = min(max_scale, scale - 0.01)
        else:
            # We weren't busy enough this time around. We never need to go as low as this scale
            # again
            min_scale = max(min_scale, scale + 0.01)

        scale += err / 100.0
        scale = min(max_scale, scale)
        scale = max(min_scale, scale)
        tries += 1


def compute_bw_for_io_multi(soc, io_scenarios):
    scale = 1.0
    tries = 1

    min_scale = 0.1
    max_scale = 2.0

    while True:
        acc = lx.Accumulator()

        for io, scenario in io_scenarios:
            scaled_scenario = {k: v * scale for k, v in scenario.items()}
            soc.add_scenarios(io, acc=acc, **scaled_scenario)

        links = busiest_links(acc)
        max_busiest_link = max(links.values())
        err = 100.0 - max_busiest_link

        # print(f'relax: {tries} scale={scale:.2f}, busiest link={max_busiest_link:.2f}, err={err:.3f}')

        if abs(err) < 1.0 or tries == 100:
            rd_bw = dram_read_bw(acc)
            wr_bw = dram_write_bw(acc)
            links.update({"DRAM RD BW": rd_bw, "DRAM WR BW": wr_bw, "DRAM BW": rd_bw + wr_bw})
            if tries == 100:
                print("failed to converge for " + " ".join([f"IO{i} = {io}" for i, (io, _) in enumerate(io_scenarios)]))
            return links, acc
        elif err < 0.0:
            # We were too busy this time around. We never need to go as high as (and certainly not
            # higher) than this current scale again
            max_scale = min(max_scale, scale - 0.01)
        else:
            # We weren't busy enough this time around. We never need to go as low as this scale
            # again
            min_scale = max(min_scale, scale + 0.01)

        scale += err / 100.0
        scale = min(max_scale, scale)
        scale = max(min_scale, scale)
        tries += 1


def compute_bw_for_io_locations(soc, io_loc, scenario1, scenario2):
    iorrs = {(c.x, c.y): c for c in soc.model.components_by_type(lx.ComponentType.IORr)}
    iorws = {(c.x, c.y): c for c in soc.model.components_by_type(lx.ComponentType.IORw)}

    m = re.match("x([0-9]+)y([0-9]+) x([0-9]+)y([0-9]+)", io_loc)
    if not m:
        raise RuntimeError(f"bad location specification: {io_loc}")

    io1_x = int(m.group(1))
    io1_y = int(m.group(2))
    io2_x = int(m.group(3))
    io2_y = int(m.group(4))

    if "ReadOnce" in scenario1:
        io1 = iorrs[(io1_x, io1_y)]
    else:
        io1 = iorws[(io1_x, io1_y)]

    if "ReadOnce" in scenario2:
        io2 = iorrs[(io2_x, io2_y)]
    else:
        io2 = iorws[(io2_x, io2_y)]

    return compute_bw_for_io_pair(soc, io1, io2, scenario1, scenario2)


def all_reads(soc, records):
    iorrs = soc.model.components_by_type(lx.ComponentType.IORr)
    for ior1, ior2 in itertools.combinations(iorrs, 2):
        record, _ = compute_bw_for_io_pair(soc, ior1, ior2, {"ReadOnce": 100}, {"ReadOnce": 100})
        record["scenario"] = "100% Read"
        record["position"] = f"x{ior1.x}y{ior1.y} x{ior2.x}y{ior2.y}"
        record["iors"] = f"{IOR_NAMES[ior1.x][ior1.y]} {IOR_NAMES[ior2.x][ior2.y]}"
        if record["DRAM RD BW"] + 1 < 256:
            records.append(record)


def all_writes(soc, records):
    iorws = soc.model.components_by_type(lx.ComponentType.IORw)
    for ior1, ior2 in itertools.combinations(iorws, 2):
        record, _ = compute_bw_for_io_pair(soc, ior1, ior2, {"WriteNoSnpPtl": 100}, {"WriteNoSnpPtl": 100})
        record["scenario"] = "100% Write"
        record["position"] = f"x{ior1.x}y{ior1.y} x{ior2.x}y{ior2.y}"
        record["iors"] = f"{IOR_NAMES[ior1.x][ior1.y]} {IOR_NAMES[ior2.x][ior2.y]}"
        if record["DRAM WR BW"] + 1 < 256:
            records.append(record)


def mixed_read_write(soc, records):
    iorrs = soc.model.components_by_type(lx.ComponentType.IORr)
    iorws = soc.model.components_by_type(lx.ComponentType.IORw)
    for iorr in iorrs:
        for iorw in iorws:
            record, _ = compute_bw_for_io_pair(soc, iorr, iorw, {"ReadOnce": 100}, {"WriteNoSnpPtl": 100})
            record["scenario"] = "50/50 Read/Write"
            record["position"] = f"x{iorr.x}y{iorr.y} x{iorw.x}y{iorw.y}"
            record["iors"] = f"{IOR_NAMES[iorr.x][iorr.y]} {IOR_NAMES[iorw.x][iorw.y]}"
            if record["DRAM RD BW"] + 1 < 128 or record["DRAM WR BW"] + 1 < 128:
                records.append(record)


def compute_bw_for_io_scenarios(soc):
    records = []
    all_reads(soc, records)
    all_writes(soc, records)
    mixed_read_write(soc, records)
    return pd.DataFrame.from_records(records)


def show_bw_io(soc):
    df = compute_bw_for_io_scenarios(soc).round()
    if len(df) == 0:
        return pn.pane.HTML("<p><b>All scenarios achieved target bandwidth</b>.</p>")
    else:
        return pn.pane.HTML(df.sort_values("scenario").set_index(["scenario", "iors", "position"]).to_html())


# %% [markdown]
# ### Hemisphere

# %%
show_bw_io(domino_hemi)

# %% [markdown]
# ### Octant 1

# %%
show_bw_io(domino_octant1_io)

# %% [markdown]
# ### Octant 2

# %%
show_bw_io(domino_octant2_io)

# %% [markdown]
# ### Octant 3

# %%
show_bw_io(domino_octant3_io)

# %% [markdown]
# ### Octant 4

# %%
show_bw_io(domino_octant4_io)


# %% [markdown]
# ### Octant Diagram

# %%
# The lines below are for visualizing issues that the IO runs above reveal. Change the IOR locations
# (the "x0y1 ..." stuff) below along with the scenario ("ReadOnce...") to one of the tests you would
# like to visualize, and this will draw the diagram of path utilizations.
#

# _, acc = compute_bw_for_io_locations(domino_octant3_io, 'x0y1 x2y1', {'ReadOnce': 100}, {'WriteNoSnpPtl': 100})
# domino_octant3_io.view2(acc)

# %% [markdown]
# ### Two RCs, Two Octants
#
# This test case involves two root complexes (RCs) both performing 100% reads _and_ 100% writes to two octants on one side of the package (so that they would naturally want to use a single ADI). In order to make this scenario operate, we actually need to create a fake set of rules that will force one of the RCs to only route to one CCL and the other to route to the other CCL.


# %%
class TwoOctantRules(UtilRules):
    def __init__(self, model, ior_1, ior_2):
        super().__init__(model)
        self.setup_octants(model, ior_1, ior_2)

    def setup_octants(self, model, ior_1, ior_2):
        pmhas = model.components_by_type(lx.ComponentType.PmHA)

        for pmha in pmhas:
            if pmha.die_id in (3, 4):
                for ior in [*ior_1, *ior_2]:
                    self.disallow(ior, pmha)
                    self.disallow(pmha, ior)
            else:
                if pmha.die_id != 1:
                    self.disallow(ior_1[0], pmha)
                    self.disallow(pmha, ior_1[0])
                    self.disallow(ior_1[1], pmha)
                    self.disallow(pmha, ior_1[1])
                elif pmha.die_id != 2:
                    self.disallow(ior_2[0], pmha)
                    self.disallow(pmha, ior_2[0])
                    self.disallow(ior_2[1], pmha)
                    self.disallow(pmha, ior_2[1])


# %%
def two_rcs_both_reading_and_writing(soc, records):
    iorrs = soc.model.components_by_type(lx.ComponentType.IORr)
    iorws = soc.model.components_by_type(lx.ComponentType.IORw)
    ior_pairs = []
    for iorr in iorrs:
        for iorw in iorws:
            if IOR_NAMES[iorr.x][iorr.y] == IOR_NAMES[iorw.x][iorw.y]:
                ior_pairs.append((iorr, iorw))

    for (ior1_r, ior1_w), (ior2_r, ior2_w) in itertools.permutations(ior_pairs, 2):
        names = [IOR_NAMES[ior1_r.x][ior1_r.y], IOR_NAMES[ior2_r.x][ior2_r.y]]
        # if int(names[0][-1]) > int(names[1][-1]):
        #    names = [names[1], names[0]]
        pair_name = " ".join(names)
        soc.rules = TwoOctantRules(soc.model, (ior1_r, ior1_w), (ior2_r, ior2_w))
        record, _ = compute_bw_for_io_multi(
            soc,
            [
                (ior1_r, {"ReadOnce": 100}),
                (ior1_w, {"WriteNoSnpPtl": 100}),
                (ior2_r, {"ReadOnce": 100}),
                (ior2_w, {"WriteNoSnpPtl": 100}),
            ],
        )
        record["scenario"] = "Read/Write IOR Pairs"
        record["position"] = ""
        record["iors"] = pair_name
        record["ior1"] = names[0]
        record["ior2"] = names[1]
        records.append(record)


def compute_bw_for_2rc_scenarios(soc):
    records = []
    two_rcs_both_reading_and_writing(soc, records)
    return pd.DataFrame.from_records(records)


def compute_bw_for_2rc_location(soc, pair1, pair2):
    iorrs = soc.model.components_by_type(lx.ComponentType.IORr)
    iorws = soc.model.components_by_type(lx.ComponentType.IORw)
    ior_pairs = []
    for iorr in iorrs:
        for iorw in iorws:
            if IOR_NAMES[iorr.x][iorr.y] == IOR_NAMES[iorw.x][iorw.y]:
                ior_pairs.append((iorr, iorw))

    for (ior1_r, ior1_w), (ior2_r, ior2_w) in itertools.permutations(ior_pairs, 2):
        pair_name = f"{IOR_NAMES[ior1_r.x][ior1_r.y]} {IOR_NAMES[ior2_r.x][ior2_r.y]}"
        if pair_name == f"{pair1} {pair2}":
            soc.rules = TwoOctantRules(soc.model, (ior1_r, ior1_w), (ior2_r, ior2_w))
            return compute_bw_for_io_multi(
                soc,
                [
                    (ior1_r, {"ReadOnce": 100}),
                    (ior1_w, {"WriteNoSnpPtl": 100}),
                    (ior2_r, {"ReadOnce": 100}),
                    (ior2_w, {"WriteNoSnpPtl": 100}),
                ],
            )

    return None, None


def show_bw_2rc(soc):
    df = compute_bw_for_2rc_scenarios(soc).round()
    if len(df) == 0:
        return pn.pane.HTML("<p><b>All scenarios achieved target bandwidth</b>.</p>")
    else:
        return pn.pane.HTML(df.sort_values("scenario").set_index(["scenario", "iors", "position"]).to_html())


# %%
domino_2rc_octant = DeadlockAndUtilizationModel(Domino_Hemi, UtilRules)

# %%
# iorrs = [iorr for iorr in domino_2rc_octant.model.components_by_type(lx.ComponentType.IORr) if iorr.x > 3]
# iorws = [iorw for iorw in domino_2rc_octant.model.components_by_type(lx.ComponentType.IORw) if iorw.x > 3]
# pmhas = [pmha for pmha in domino_2rc_octant.model.components_by_type(lx.ComponentType.PmHA) if pmha.die_id in (1,2)]
# domino_2rc_octant.show_routes(lx.Channel.REQ, iorrs + iorws, pmhas)

# %%
df_2rc = compute_bw_for_2rc_scenarios(domino_2rc_octant)

# %%
df_2rc["DRAM BW"] = df_2rc["DRAM BW"]
(
    df_2rc[["ior1", "ior2", "DRAM BW"]]
    .pivot(columns="ior2", index="ior1", values="DRAM BW")
    .style.background_gradient(cmap="coolwarm_r", axis=None, vmin=256, vmax=400)
    .highlight_null(color="#f1f1f1")
    .format(precision=0)
)

# %%
_, acc = compute_bw_for_2rc_location(domino_2rc_octant, "IOR4", "IOR5")
domino_2rc_octant.view2(acc)


# %% [markdown]
# ## IO Peer-to-Peer Scenarios


# %%
def peer_to_peer_read_bw(acc, reader):
    bw = 0.0
    for (from_comp, to_comp), per_opcode_traffic in acc.as_dict(lx.Channel.DAT).items():
        if to_comp.type not in {lx.ComponentType.IOR, lx.ComponentType.IORr}:
            continue
        if (to_comp.x, to_comp.y) == (reader.x, reader.y):
            link_traffic = sum(
                [traffic for opcode, traffic in per_opcode_traffic.items() if opcode == lx.Opcode.CompData]
            )
            link_bw = link_traffic / 100.0 * 64.0 * 2.0
            bw += link_bw
    return bw


def peer_to_peer_write_bw(acc, writer):
    bw = 0.0
    for (from_comp, to_comp), per_opcode_traffic in acc.as_dict(lx.Channel.DAT).items():
        if from_comp.type not in {lx.ComponentType.IOR, lx.ComponentType.IORw}:
            continue
        if (from_comp.x, from_comp.y) == (writer.x, writer.y):
            link_traffic = sum(
                [traffic for opcode, traffic in per_opcode_traffic.items() if opcode == lx.Opcode.NonCopyBackWrData]
            )
            link_bw = link_traffic / 100.0 * 64.0 * 2.0
            bw += link_bw
    return bw


def compute_bw_for_io_pair_peer_to_peer(soc, ior1, ioh1, ior2, ioh2, scenario1, scenario2):
    scale = 1.0
    tries = 1

    min_scale = 0.1
    max_scale = 2.0

    while True:
        scaled_scenario1 = {k: v * scale for k, v in scenario1.items()}
        scaled_scenario2 = {k: v * scale for k, v in scenario2.items()}

        acc = soc.add_peer_to_peer_scenarios(ior1, ioh1, **scaled_scenario1)
        soc.add_peer_to_peer_scenarios(ior2, ioh2, acc=acc, **scaled_scenario2)

        links = busiest_links(acc)
        max_busiest_link = max(links.values())
        err = 100.0 - max_busiest_link

        # print(f'relax: {tries} scale={scale:.2f}, busiest link={max_busiest_link:.2f}, err={err:.3f}')

        if abs(err) < 1.0 or tries == 100:
            links.update(
                {
                    "IO1 RD BW": peer_to_peer_read_bw(acc, ior1),
                    "IO2 RD BW": peer_to_peer_read_bw(acc, ior2),
                    "IO1 WR BW": peer_to_peer_write_bw(acc, ior1),
                    "IO2 WR BW": peer_to_peer_write_bw(acc, ior2),
                }
            )
            if tries == 10:
                print(f"failed to converge for IO1 = {io1}, IO2 = {io2}")
            return links, acc
        elif err < 0.0:
            # We were too busy this time around. We never need to go as high as (and certainly not
            # higher) than this current scale again
            max_scale = min(max_scale, scale - 0.01)
        else:
            # We weren't busy enough this time around. We never need to go as low as this scale
            # again
            min_scale = max(min_scale, scale + 0.01)

        scale += err / 100.0
        scale = min(max_scale, scale)
        scale = max(min_scale, scale)
        tries += 1


def compute_bw_for_io_locations_peer_to_peer(soc, io_loc1, io_loc2, scenario1, scenario2):
    iorrs = {(c.x, c.y): c for c in soc.model.components_by_type(lx.ComponentType.IORr)}
    iorws = {(c.x, c.y): c for c in soc.model.components_by_type(lx.ComponentType.IORw)}
    iohrs = {(c.x, c.y): c for c in soc.model.components_by_type(lx.ComponentType.IOHr)}
    iohws = {(c.x, c.y): c for c in soc.model.components_by_type(lx.ComponentType.IOHw)}

    m = re.match("x([0-9]+)y([0-9]+) x([0-9]+)y([0-9]+)", io_loc1)
    if not m:
        raise RuntimeError(f"bad location specification: {io_loc1}")

    ior1_x = int(m.group(1))
    ior1_y = int(m.group(2))
    ioh1_x = int(m.group(3))
    ioh1_y = int(m.group(4))

    m = re.match("x([0-9]+)y([0-9]+) x([0-9]+)y([0-9]+)", io_loc2)
    if not m:
        raise RuntimeError(f"bad location specification: {io_loc2}")

    ior2_x = int(m.group(1))
    ior2_y = int(m.group(2))
    ioh2_x = int(m.group(3))
    ioh2_y = int(m.group(4))

    if "ReadNoSnp_P2P" in scenario1:
        ior1 = iorrs[(ior1_x, ior1_y)]
        ioh1 = iohrs[(ioh1_x, ioh1_y)]
    else:
        ior1 = iorws[(ior1_x, ior1_y)]
        ioh1 = iohws[(ioh1_x, ioh1_y)]

    if "ReadNoSnp_P2P" in scenario2:
        ior2 = iorrs[(ior2_x, ior2_y)]
        ioh2 = iohrs[(ioh2_x, ioh2_y)]
    else:
        ior2 = iorws[(ior2_x, ior2_y)]
        ioh2 = iohws[(ioh2_x, ioh2_y)]

    return compute_bw_for_io_pair_peer_to_peer(soc, ior1, ioh1, ior2, ioh2, scenario1, scenario2)


def all_reads_peer_to_peer(soc, records):
    peers = [
        (ior, ioh)
        for ior in soc.model.components_by_type(lx.ComponentType.IORr)
        for ioh in soc.model.components_by_type(lx.ComponentType.IOHr)
    ]

    for peers1, peers2 in itertools.combinations(peers, 2):
        if peers1[0] == peers2[0] or peers1[1] == peers2[1]:
            continue
        ior1, ioh1 = peers1
        ior2, ioh2 = peers2
        record, _ = compute_bw_for_io_pair_peer_to_peer(
            soc, ior1, ioh1, ior2, ioh2, {"ReadNoSnp_P2P": 100}, {"ReadNoSnp_P2P": 100}
        )
        record["scenario"] = "100% Read"
        record["iors1"] = f"{IOR_NAMES[ior1.x][ior1.y]} {IOR_NAMES[ioh1.x][ioh1.y]}"
        record["iors2"] = f"{IOR_NAMES[ior2.x][ior2.y]} {IOR_NAMES[ioh2.x][ioh2.y]}"
        record["peers1"] = f"x{ior1.x}y{ior1.y} x{ioh1.x}y{ioh1.y}"
        record["peers2"] = f"x{ior2.x}y{ior2.y} x{ioh2.x}y{ioh2.y}"
        if not (record["IO1 RD BW"] == 128.0 and record["IO2 RD BW"] == 128.0) and record["DAT"] + 1 > 100.0:
            records.append(record)


def all_writes_peer_to_peer(soc, records):
    peers = [
        (ior, ioh)
        for ior in soc.model.components_by_type(lx.ComponentType.IORw)
        for ioh in soc.model.components_by_type(lx.ComponentType.IOHw)
    ]

    for peers1, peers2 in itertools.combinations(peers, 2):
        if peers1[0] == peers2[0] or peers1[1] == peers2[1]:
            continue
        ior1, ioh1 = peers1
        ior2, ioh2 = peers2
        record, _ = compute_bw_for_io_pair_peer_to_peer(
            soc, ior1, ioh1, ior2, ioh2, {"WriteNoSnp_P2P": 100}, {"WriteNoSnp_P2P": 100}
        )
        record["scenario"] = "100% Write"
        record["iors1"] = f"{IOR_NAMES[ior1.x][ior1.y]} {IOR_NAMES[ioh1.x][ioh1.y]}"
        record["iors2"] = f"{IOR_NAMES[ior2.x][ior2.y]} {IOR_NAMES[ioh2.x][ioh2.y]}"
        record["peers1"] = f"x{ior1.x}y{ior1.y} x{ioh1.x}y{ioh1.y}"
        record["peers2"] = f"x{ior2.x}y{ior2.y} x{ioh2.x}y{ioh2.y}"
        if not (record["IO1 WR BW"] == 128.0 and record["IO2 WR BW"] == 128.0) and record["DAT"] + 1 > 100.0:
            records.append(record)


def mixed_read_write_peer_to_peer(soc, records):
    read_peers = [
        (ior, ioh)
        for ior in soc.model.components_by_type(lx.ComponentType.IORr)
        for ioh in soc.model.components_by_type(lx.ComponentType.IOHr)
    ]
    write_peers = [
        (ior, ioh)
        for ior in soc.model.components_by_type(lx.ComponentType.IORw)
        for ioh in soc.model.components_by_type(lx.ComponentType.IOHw)
    ]

    for read_peer in read_peers:
        for write_peer in write_peers:
            iorr, iohr = read_peer
            iorw, iohw = write_peer

            iorr_name = IOR_NAMES[iorr.x][iorr.y]
            iohr_name = IOR_NAMES[iohr.x][iohr.y]
            iorw_name = IOR_NAMES[iorw.x][iorw.y]
            iohw_name = IOR_NAMES[iohw.x][iohw.y]

            if iorr_name == iorw_name or iohr_name == iohw_name:
                continue

            record, _ = compute_bw_for_io_pair_peer_to_peer(
                soc, iorr, iohr, iorw, iohw, {"ReadNoSnp_P2P": 100}, {"WriteNoSnp_P2P": 100}
            )
            record["scenario"] = "50/50 Read/Write"
            record["iors1"] = f"{iorr_name} {iohr_name}"
            record["iors2"] = f"{iorw_name} {iohw_name}"
            record["peers1"] = f"x{iorr.x}y{iorr.y} x{iohr.x}y{iohr.y}"
            record["peers2"] = f"x{iorw.x}y{iorw.y} x{iohw.x}y{iohw.y}"

            if not (record["IO1 RD BW"] == 128.0 and record["IO2 WR BW"] == 128.0):
                records.append(record)


def compute_bw_for_io_peer_to_peer_scenarios(soc):
    records = []
    all_reads_peer_to_peer(soc, records)
    all_writes_peer_to_peer(soc, records)
    mixed_read_write_peer_to_peer(soc, records)
    return pd.DataFrame.from_records(records)


def show_bw_io_peer_to_peer(soc):
    df = compute_bw_for_io_peer_to_peer_scenarios(soc)
    print(len(df))
    return pn.pane.HTML(df.sort_values("scenario").set_index(["scenario", "iors1", "iors2"]).to_html())


# %%
# show_bw_io_peer_to_peer(domino_iod_only)

# %%
# _, acc = compute_bw_for_io_locations_peer_to_peer(domino_iod_only, 'x5y2 x2y2', 'x0y2 x7y2', {'ReadNoSnp_P2P': 100}, {'WriteNoSnp_P2P': 100})
# domino_iod_only.view2(acc)

# %% [markdown]
# # Reference

# %%
# with open("model.json", "w") as model:
#    print(domino_hemi.model.dump_json(), file=model)

# %%
# with open("rules.json", "w") as rules:
#    print(domino_hemi.rules.dump_json(), file=rules)
