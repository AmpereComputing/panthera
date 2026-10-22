#! /usr/bin/env python3
# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Generate a Trace Event JSON file from an event frame.

The Trace Event file can be viewed in Google chrome or the chromium browser.

Just open a tab in the browser and navigate to chrome://tracing. Then use the load
button to load the trace event file.

"""

import gzip
import json
import logging
import os
import sys

import panthera.events.trace_events as te
from panthera import ArgumentParser, setup_logging
from panthera.streams.event_stream import EventStreamReader

from . import rename_spec
from .mapping import Mapper

# Default hex arg fields
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


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__, tool="events.tracing", comments=True)
    parser.add_argument("input", nargs="?", default="events.es", help="event stream file")
    parser.add_argument(
        "--view", default="resource", help="type of view to generate", choices=["resource", "transactions"]
    )
    parser.add_argument(
        "-e",
        "--event",
        metavar="GLOB",
        default=[],
        action="append",
        type=te.trace_spec,
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
        "--period", action="store", help="clock period in picoseconds", type=int, default=te.DEFAULT_PERIOD
    )
    parser.add_argument("-o", "--output", help="output file")
    parser.add_argument(
        "-p",
        "--pipe-mode",
        dest="pipe_mode",
        action="store_true",
        help="ignore input and output file arguments and use stdin and stdout instead",
    )
    parser.add_argument("--debug", dest="debug", action="store_true", help="set log level to DEBUG instead of INFO")
    parser.add_argument("-c", "--compress", action="store_true")
    parser.add_argument(
        "--no-sort-keys",
        dest="sort_keys",
        action="store_false",
        default=True,
        help="Disable sorting of keys in JSON output",
    )

    parser.add_argument("--tx_size", help="transaction buffer size", default=0)
    parser.add_argument("--tx_index", help="transaction index data field", default="txid")
    parser.add_argument("--tx_name", help="transaction name data field")
    parser.add_argument("--tx_start", help="transaction start event", type=te.trace_spec, default=None)
    parser.add_argument("--tx_end", help="transaction end event", type=te.trace_spec, default=None)

    parser.add_argument(
        "--post_to_tx",
        action="append",
        default=[],
        help='Post data from an event to the containing transaction (syntax: "<event>:<data>[=<alias>]"',
    )
    parser.add_argument(
        "--pull_from_tx",
        action="append",
        default=[],
        help='Pull data from the transaction to an event (syntax: "<event>:<data/alias>")',
    )

    parser.add_argument(
        "--split",
        action="append",
        type=te.split_spec,
        default=[],
        help='Split an event into multiple events based on data values (syntax: "<event>/<data>[/<data>...]=<format>", where {{<data>}} with the data name will insert the value for that split into the name.',
    )
    parser.add_argument(
        "--split-all",
        action="append",
        type=te.split_spec,
        default=[],
        help='Split and include an event into multiple events based on data values (syntax: "<event>/<data>[/<data>...]=<format>", where \\{<data>\\} with the data name will insert the value for that split into the name.',
    )
    parser.add_argument(
        "-r",
        "--rename",
        action="append",
        type=rename_spec,
        default=[],
        help='Rename event using regular expression-like syntax: <event>=<newname> (example: "pmu_split_(#)=pmu.something.\1")',
    )
    parser.add_argument(
        "-x",
        "--exclude",
        action="append",
        type=te.trace_spec,
        default=[],
        help="Exclude event",
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

    args = parser.parse_args()

    setup_logging(debug=args.debug)

    logging.debug("hex args: '{}'".format(args.hex))
    logging.debug("instruction args: '{}'".format(args.instruction))

    mapper = Mapper(args.event_group, args.hex, args.instruction)
    if args.pipe_mode:
        logging.debug("reading from stdin")
        es = EventStreamReader(sys.stdin.fileno(), all_events=True, convert_enumerations=args.convert_enumerations)
    else:
        logging.debug("reading {}".format(args.input))
        es = EventStreamReader(args.input, all_events=True, convert_enumerations=args.convert_enumerations)

    events = {e: None for e in te.TraceEventStream.TRANSACTION_EVENTS}
    for i, counter in enumerate(args.event):
        events[counter.event] = i

    _event_filters = []
    if args.split:
        _event_filters.append(te.EventSplitter(args.split))
    if args.split_all:
        _event_filters.append(te.EventSplitter(args.split_all, include=True))
    if args.rename:
        _event_filters.append(te.EventRenamer(args.rename))
    if args.event and args.split:
        _event_filters.append(te.EventIncluder(args.event))
    if args.exclude:
        _event_filters.append(te.EventExcluder(args.exclude))
    event_filters = te.EventFilters(_event_filters, default_inclusion=not (args.event and args.split))

    for event in event_filters.events():
        if event.event not in events:
            events[event.event] = None

    if args.view == "resource":
        tes = te.ResourceView(
            mapper,
            events,
            args.period,
            occupancy_list=args.occupancy,
            post_to_tx=args.post_to_tx,
            pull_from_tx=args.pull_from_tx,
            event_filters=event_filters,
        )
    else:
        assert args.view == "transactions"
        if args.tx_start:
            events[args.tx_start.event] = None
        if args.tx_end:
            events[args.tx_end.event] = None

        tes = te.TransactionBuffer(
            mapper,
            events,
            int(args.tx_size),
            args.tx_index,
            name_field=args.tx_name,
            start_event=args.tx_start.event,
            end_event=args.tx_end.event,
            period=args.period,
            event_filters=event_filters,
        )

    logging.debug("processing")
    tes.process_event_stream(es)

    if not args.output:
        stem, _ = os.path.splitext(os.path.basename(args.input))
        output = f"{stem}.json"
    else:
        output = args.output

    if args.compress and not output.endswith(".gz"):
        output += ".gz"

    def dump_json(file, readable=True):
        json_args = {"sort_keys": args.sort_keys}
        if readable:
            json_args["indent"] = 2

        return json.dump(tes.trace_events(), file, **json_args)

    if args.pipe_mode:
        logging.debug("writing to stdout")
        dump_json(sys.stdout)
        sys.stdout.close()
        sys.stdin.close()
    else:
        logging.debug("writing {}".format(output))
        if args.compress:
            with gzip.open(output, "wt") as f:
                dump_json(f, readable=False)
        else:
            with open(output, "w") as f:
                dump_json(f)
