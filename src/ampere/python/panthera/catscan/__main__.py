#!/usr/bin/env python3

# Copyright (c) 2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import asyncio
import logging
import os
import warnings

import urwid

import panthera.events as events
import panthera.events.trace_events as trace_events
from panthera import ArgumentParser
from panthera.catscan.colors import palette
from panthera.catscan.widgets.top import Top
from panthera.events.mapping import Mapper

HEXARGS_DEFAULT = [
    "program_counter",
    "virtual_address",
    "physical_address",
    "phys_line",
    r"\S+\.br_pc",
    r"\S+\.br_prev_pc",
    r"\S+\.br_target",
]
# Default instruction fields
INSTARGS_DEFAULT = ["core.icf.inst_bytes", "sys.soc_0.core_0.gpc.commit_instruction"]


def main(args):
    top = Top(args)
    event_loop = asyncio.get_event_loop()
    event_loop.set_debug(args.debug)
    loop = urwid.MainLoop(
        urwid.AttrMap(top, "body"),
        palette=palette,
        event_loop=urwid.AsyncioEventLoop(loop=event_loop),
    )
    top.main_loop = loop  # Allow 'top' to reference the main loop (yuck!)

    # Try to use 256 colors if we think the terminal supports it
    colors = 16
    if "TERM" in os.environ:
        if "256" in os.environ["TERM"]:
            colors = 256
    loop.screen.set_terminal_properties(colors)
    loop.screen.reset_default_terminal_palette()
    loop.screen.focus_reporting = True

    mapper = Mapper(args.event_group, [], args.instruction, inline_for_search=False)
    event_filters = []
    if args.split:
        event_filters.append(trace_events.EventSplitter(args.split))
    if args.split_all:
        event_filters.append(trace_events.EventSplitter(args.split_all, include=True))
    if args.rename:
        event_filters.append(trace_events.EventRenamer(args.rename))
    if args.event and args.split:
        event_filters.append(trace_events.EventIncluder(args.event))
    if args.exclude:
        event_filters.append(trace_events.EventExcluder(args.exclude))

    top.load_file(
        args.input,
        args.view,
        mapper,
        trace_events.EventFilters(event_filters, default_inclusion=not (args.event and args.split)),
        args.event,
        args.cache,
        args.post_to_tx,
        args.pull_from_tx,
        occupancy=args.occupancy,
        transaction_name=args.tx_name,
        transaction_start=args.tx_start,
        transaction_end=args.tx_end,
        convert_enumerations=args.convert_enumerations,
    )

    loop.run()


if __name__ == "__main__":
    global hexargs_re

    parser = ArgumentParser(description=__doc__, tool="catscan", tool_aliases=["events.tracing"], comments=True)
    parser.add_argument("input", nargs="?", default="events.es", help="event stream file")
    parser.add_argument("--log", default=None, help="Log to specified filename")
    parser.add_argument(
        "--cache",
        nargs="?",
        default=None,
        const="",
        help='Cache a "pickled" version of this event stream, once loaded (supply path to override default)',
    )
    parser.add_argument("--view", default="unspecified", help="type of view to generate")
    parser.add_argument(
        "--onload-command",
        default=[],
        action="append",
        help="commands to execute as soon as the event stream is loaded",
    )
    parser.add_argument(
        "--onsync-command",
        default=[],
        action="append",
        help="commands to execute when a new sync session is fully initialized",
    )
    parser.add_argument(
        "-e",
        "--event",
        metavar="GLOB",
        default=[],
        action="append",
        type=trace_events.trace_spec,
        help="event to include in output",
    )
    parser.add_argument(
        "--occupancy", metavar="NAME:START,END(,DATA)", default=[], action="append", help="occupancy of a buffer"
    )
    parser.add_argument(
        "--hex",
        metavar="REGEX",
        default=HEXARGS_DEFAULT,
        action="append",
        help="data field to display in hexadecimal format",
    )
    parser.add_argument(
        "--instruction",
        metavar="REGEX",
        default=INSTARGS_DEFAULT,
        action="append",
        help="data field to treat as an instruction and run disassembly on, also applies hex format",
    )
    parser.add_argument(
        "--period", action="store", help="clock period in picoseconds", type=int, default=trace_events.DEFAULT_PERIOD
    )
    parser.add_argument(
        "--no-sort-keys",
        dest="sort_keys",
        action="store_false",
        default=True,
        help="Disable sorting of keys in JSON output",
    )
    parser.add_argument(
        "--post_to_tx",
        action="append",
        default=[],
        help='Post data from an event to the containing transaction (syntax: "<event>:<data>[=<alias>]"). Data which is not pulled from will be displayed under an events transcation data in the sidebar.',
    )
    parser.add_argument(
        "--pull_from_tx",
        action="append",
        default=[],
        help='Pull data from the transaction to an event (syntax: "<event>:<data/alias>")',
    )

    parser.add_argument("--tx_size", type=int, help="transaction buffer size", default=0)
    parser.add_argument("--tx_index", help="transaction index data field", default="txid")
    parser.add_argument("--tx_name", action="append", help="transaction name data field")
    parser.add_argument(
        "--tx_start", action="append", help="transaction start event", type=trace_events.trace_spec, default=None
    )
    parser.add_argument(
        "--tx_end", action="append", help="transaction end event", type=trace_events.trace_spec, default=None
    )

    parser.add_argument(
        "--split",
        action="append",
        type=trace_events.split_spec,
        default=[],
        help='Split an event into multiple events based on data values (syntax: "<event>/<data>[/<data>...]=<format>", where \\{<data>\\} with the data name will insert the value for that split into the name.',
    )
    parser.add_argument(
        "--split-all",
        action="append",
        type=trace_events.split_spec,
        default=[],
        help='Split and include an event into multiple events based on data values (syntax: "<event>/<data>[/<data>...]=<format>", where \\{<data>\\} with the data name will insert the value for that split into the name.',
    )
    parser.add_argument(
        "-r",
        "--rename",
        action="append",
        type=events.rename_spec,
        default=[],
        help='Rename event using regular expression-like syntax: <event>=<newname> (example: "pmu_split_(#)=pmu.something.\1")',
    )
    parser.add_argument(
        "-x",
        "--exclude",
        action="append",
        type=trace_events.trace_spec,
        default=[],
        help="Exclude event",
    )
    parser.add_argument(
        "--instruction-commit-event",
        default="sys.soc_0.core_0.gpc.instruction_commit",
        help="The name of the event corresponding to instruction commit",
    )
    parser.add_argument(
        "--instruction-commit-index",
        default="sys.soc_0.core_0.gpc.inum",
        help='The name of the event data item representing the committed instruction index (a.k.a. "inum")',
    )

    parser.add_argument(
        "-g", "--event-group", action="append", default=[], help="Generate a new group for this event prefix"
    )
    parser.add_argument(
        "--no-enums",
        dest="convert_enumerations",
        default=True,
        action="store_false",
        help="Don't convert enumerations from ES (i.e. just use mapper)",
    )
    parser.add_argument("--debug", action="store_true", help="Debugging logging info")

    args = parser.parse_args()

    if args.log:
        logging.basicConfig(
            format="[%(relativeCreated)d] %(levelname)s:%(name)s %(message)s",
            filename=args.log,
            encoding="utf-8",
            level=logging.DEBUG if args.debug else logging.INFO,
        )

    def warn_unsupported_arg(name):
        warnings.warn(f"--{name} is unsupported, anything specified is being ignored")

    # Warn user about options we don't support (yet!)
    if args.tx_size > 0:
        warn_unsupported_arg("tx_size")
    if len(args.tx_index) > 0:
        warn_unsupported_arg("tx_index")

    main(args)
