# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from collections import defaultdict
from collections.abc import Iterable

import matplotlib as mpl
import panel as pn

import panthera.lynx as lx

from .annotations import (
    Annotations,
    accumulator_to_annotations,
    contributing_paths_to_annotations,
    cycle_to_annotations,
)
from .render import SVGRenderer

svg = SVGRenderer()
inferno = mpl.colormaps["inferno"]
accent = mpl.colormaps["Dark2"]


class DeadlockAndUtilizationModel:
    """DeadlockAndUtilizationModel is an umbrella class for managing utilization
    experiments and deadlock checks on an SoC. It has three main parts:

        1. The SoC object. This will come from a product specific library, for
        example `matrix.domino.Domino` is a such a child of `SoC`.

        2. The Lynx model. The SoC is converted into a Lynx model representation
        in order to do fast deadlock checks and traffic analysis.

        3. The Rules object, which describes how routing is done for this
        particular model. A Rules object is also partly in the Python realm and
        partly in the Lynx realm.
    """

    CHANNEL = {
        "req": lx.Channel.REQ,
        "rsp0": lx.Channel.RSP0,
        "rsp1": lx.Channel.RSP1,
        "snp": lx.Channel.SNP,
        "dat": lx.Channel.DAT,
    }

    def __init__(self, soc_type, rules_type, display=None):
        """Create a DeadlockAndUtilizationModel based on the supplied types:

            `soc_type`    : The class (_not_ the object) of SoC to build.
            `rules_type`  : The class (_not_ the object) of the Rules set we want to apply.

        Optionally:

            `display`     : An alternate SoC class that will be used for rendering.
                            This can be useful if you want to run analysis on a full
                            SoC, but you know you only want to display, say, one
                            particular die when showing diagrams.
        """
        self.soc = soc_type()
        if display is None:
            self.draw_soc = self.soc
        else:
            self.draw_soc = display()
        self.model = lx.Model()
        self.soc.to_lynx(
            self.model, omit_colocated_devices=set([lx.ComponentType.Core, lx.ComponentType.PxHA, lx.ComponentType.MCU])
        )
        self.rules = rules_type(self.model)

        self.scenarios = defaultdict(lambda: 0.0)
        self.sources = [
            c for c in self.soc.dev_by_type["Core"] if c.didx == 0
        ]  # throw out all cores except those with device index 0, so we don't eval so much
        self.chan_selector = pn.widgets.Select(
            name="Channel", value="req", options=["req", "rsp0", "rsp1", "snp", "dat"]
        )
        self.deadlocks = None

    def find_deadlocks(self):
        """Run deadlock detection on all channels.

        The resulting cycles (if any) are put into the `deadlocks` dictionary
        in this object, indexed by channel. Only one (the shortest) cycle is
        recorded per channel. Also the smallest set of contributing paths
        (meaning the end-to-end src->tgt paths) are stored in
        `deadlock_contributing_paths`, which is also indexed by channel.

        Normally the user would call `check_deadlocks` or `show_deadlocks`
        instead of calling this routing directly, because they provide some
        useful output.
        """
        self.deadlocks = {}
        self.deadlock_contributing_paths = {}
        for chan in (lx.Channel.REQ, lx.Channel.RSP0, lx.Channel.RSP1, lx.Channel.SNP, lx.Channel.DAT):
            g = lx.build_link_graph(self.model, self.rules, chan)
            self.deadlocks[chan] = g.find_cycle()
            if self.deadlocks[chan]:
                self.deadlock_contributing_paths[chan] = g.find_cycle_with_contributing_paths()

    def check_deadlocks(self):
        """Check deadlocks on all channels. Print the final result for each channel."""
        if self.deadlocks is None:
            self.find_deadlocks()
        for chan in (lx.Channel.REQ, lx.Channel.RSP0, lx.Channel.RSP1, lx.Channel.SNP, lx.Channel.DAT):
            if self.deadlocks[chan]:
                print(f"channel {chan} has deadlocks")
            else:
                print(f"channel {chan} is deadlock free")

    def show_deadlocks(self, channel, scale=0.45):
        """Draw a deadlock diagram for the given channel.

        This will run deadlock detection if it hasn't already been run. Then it will render
        the SoC with both the deadlock cycle (links highlighted in red) and the contributing
        paths (drawn as translucent overlying arrows).
        """
        if self.deadlocks is None:
            self.find_deadlocks()
        channel = self.CHANNEL[channel]
        svg = SVGRenderer()
        if self.deadlocks[channel]:
            ann = cycle_to_annotations(self.deadlocks[channel])
            contributing_paths_to_annotations(self.deadlock_contributing_paths[channel], ann)
            return pn.pane.HTML(svg(self.draw_soc, annotations=ann, scale=scale))
        else:
            return pn.pane.HTML(svg(self.draw_soc, scale=scale))

    def show_routes(self, channel, src, tgt=None, scale=0.45):
        """This attempts to draw all routes from a source to all targets for a given channel."""
        routes = []
        if not isinstance(src, Iterable):
            sources = [src]
        else:
            sources = src
        if tgt is None:
            targets = self.model.all_targets(self.rules, src, channel)
        elif isinstance(tgt, Iterable):
            targets = tgt
        else:
            targets = [tgt]
        for src in sources:
            for tgt in targets:
                routes += lx.paths_to_target(self.model, src, tgt, channel, self.rules)
        ann = contributing_paths_to_annotations(routes)
        return pn.pane.HTML(svg(self.draw_soc, annotations=ann, scale=scale))

    def _all_sources(self):
        """Gather up interesting sources from a model. Used by `show_routes`, which as
        that docstring explains, hasn't actually been useful.
        """
        comp_types = (lx.ComponentType.Core, lx.ComponentType.PxHA, lx.ComponentType.PmHA, lx.ComponentType.MCU)
        srcs = []
        for comp_type in comp_types:
            srcs += self.model.components_by_type(comp_type)
        return srcs

    def _diag_one_channel(self, acc, channel):
        """Draw a diagram of the SoC annotated with traffic on the given channel."""
        ann = Annotations()
        accumulator_to_annotations(acc, self.CHANNEL[channel], ann)
        self.format_values(ann, "label", inferno)
        svg = SVGRenderer()
        return pn.pane.HTML(svg(self.draw_soc, annotations=ann, scale=0.6))

    def calc_utilization(self, scenarios=None, **kwargs):
        """Calculate utilization for the given scenarios as originating
        from all cores.

        Returns the Accumulator.

        FIXME: We should be able to make this more generic---as in, a
        routine that can calculate utilization from one or more sources
        to one or more targets, instead of assuming cores and memory.
        """
        if scenarios is None:
            scenarios = defaultdict(lambda: 0.0)
            scenarios.update(kwargs)

        acc = lx.Accumulator()

        for source in self.sources:
            self.add_scenarios(source, acc, scenarios)

        return acc

    def add_scenarios(self, source, acc=None, scenarios=None, **kwargs):
        """Accumulate traffic from a given source for the given scenarios.

        We can build scenarios from the keyword arguments if no scenarios
        are supplied.

        Return the Accumulator.
        """
        if scenarios is None:
            scenarios = defaultdict(lambda: 0.0)
            scenarios.update(kwargs)

        if acc is None:
            acc = lx.Accumulator()

        if scenarios["ReadShared"] > 0.0:
            source.ReadShared(self.model, self.rules, scenarios["ReadShared"], False, acc)
        if scenarios["ReadShared L3 Hit"] > 0.0:
            source.ReadShared(self.model, self.rules, scenarios["ReadShared L3 Hit"], True, acc)
        if scenarios["ReadUnique"] > 0.0:
            source.ReadUnique(self.model, self.rules, scenarios["ReadUnique"], False, acc)
        if scenarios["ReadUnique L3 Hit"] > 0.0:
            source.ReadUnique(self.model, self.rules, scenarios["ReadUnique L3 Hit"], True, acc)
        if scenarios["MakeUnique"] > 0.0:
            source.MakeUnique(self.model, self.rules, scenarios["MakeUnique"], acc)
        if scenarios["WriteEvictFull"] > 0.0:
            source.WriteEvictFull(self.model, self.rules, scenarios["WriteEvictFull"], acc)
        if scenarios["Evict"] > 0.0:
            source.Evict(self.model, self.rules, scenarios["Evict"], acc)
        if scenarios["WriteBackFull"] > 0.0:
            source.WriteBackFull(self.model, self.rules, scenarios["WriteBackFull"], False, acc)
        if scenarios["WriteBackFull L3"] > 0.0:
            source.WriteBackFull(self.model, self.rules, scenarios["WriteBackFull L3"], True, acc)
        if scenarios["ReadOnce"] > 0.0:
            source.ReadOnce(self.model, self.rules, scenarios["ReadOnce"], acc)
        if scenarios["WriteNoSnpPtl"] > 0.0:
            source.WriteNoSnpPtl(self.model, self.rules, scenarios["WriteNoSnpPtl"], acc)

        return acc

    def add_cxl_scenarios(self, source, target, acc=None, scenarios=None, **kwargs):
        """Run scenarios for cores targeting CXL devices. This is similar to the peer-to-peer
        IO case in that it uses a specific target."""
        if scenarios is None:
            scenarios = defaultdict(lambda: 0.0)
            scenarios.update(kwargs)

        if acc is None:
            acc = lx.Accumulator()

        if scenarios["ReadShared CXL"] > 0.0:
            source.ReadShared_CXL(target, self.model, self.rules, scenarios["ReadShared CXL"], acc)
        if scenarios["ReadUnique CXL"] > 0.0:
            source.ReadUnique_CXL(target, self.model, self.rules, scenarios["ReadUnique CXL"], acc)

        return acc

    def add_peer_to_peer_scenarios(self, source, target, acc=None, scenarios=None, **kwargs):
        """Run scenarious for peer-to-peer IO (that is, IOR -> IOH requests)."""
        if scenarios is None:
            scenarios = defaultdict(lambda: 0.0)
            scenarios.update(kwargs)

        if acc is None:
            acc = lx.Accumulator()

        if scenarios["ReadNoSnp_P2P"] > 0.0:
            source.ReadNoSnp_P2P(target, self.model, self.rules, scenarios["ReadNoSnp_P2P"], acc)
        if scenarios["WriteNoSnp_P2P"] > 0.0:
            source.WriteNoSnp_P2P(target, self.model, self.rules, scenarios["WriteNoSnp_P2P"], acc)

        return acc

    def _diag(self):
        """Draw a utilization diagram based on scenarios we have already collected."""
        acc = self.calc_utilization(self.scenarios)
        return pn.bind(DeadlockAndUtilizationModel._diag_one_channel, self, acc=acc, channel=self.chan_selector)

    def view(self, **kwargs):
        """Draw a utilization diagram for scenarios supplied in the keyword arguments."""
        self.scenarios.clear()
        self.scenarios.update(kwargs)
        return pn.Row(self.chan_selector, self._diag())

    def view2(self, acc):
        """Alternate version of `view` that uses an already populated Accumulator instead
        of the keyword arguments."""
        return pn.Column(
            self.chan_selector,
            pn.bind(DeadlockAndUtilizationModel._diag_one_channel, self, acc=acc, channel=self.chan_selector),
        )

    def deadlock_view(self, scale=0.45):
        """View deadlocks along with the channel selector to control the view."""
        return pn.Column(
            self.chan_selector,
            pn.bind(DeadlockAndUtilizationModel.show_deadlocks, self, self.chan_selector, scale=scale),
        )

    @staticmethod
    def format_values_helper(d, target_key, colormap, m):
        value = None

        for k, v in d.items():
            if k == target_key:
                value = v
                if v < 10.0:
                    fv = f"{v:0.1f}"
                else:
                    fv = f"{v:0.0f}"
                d[k] = fv
            elif isinstance(v, Annotations):
                DeadlockAndUtilizationModel.format_values_helper(v, target_key, colormap, m)

        if value is not None:
            r, g, b, _ = colormap(value / m, bytes=True)
            d["color"] = f"rgb({r} {g} {b})"

            # Build the "title" (link tooltip) from the traffic per opcode
            if "traffic_by_opcode" in d:
                total_traffic = sum(d["traffic_by_opcode"].values())
                pct_traffic_by_opcode = {
                    opcode: f"{opcode}: {100 * traffic / total_traffic:0.1f}%"
                    for opcode, traffic in d["traffic_by_opcode"].items()
                }
                d["title"] = " ".join([pct_traffic_by_opcode[op] for op in sorted(pct_traffic_by_opcode.keys())])

    @staticmethod
    def format_values(d, target_key, colormap):
        DeadlockAndUtilizationModel.format_values_helper(d, target_key, colormap, 90.0)
