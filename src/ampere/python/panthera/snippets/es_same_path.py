#!/usr/bin/env python3
# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Validate that two event stream files represent the same path of committed instructions.
"""

import argparse
import functools
import logging
import os
import sys
from itertools import zip_longest

import panthera.streams.event_stream_pb2 as es_proto
from panthera.streams.event_stream import EventStreamReader

from .. import setup_logging


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("es1", help="First event stream")
    parser.add_argument("es2", help="Second event stream")
    parser.add_argument(
        "--es1-instr-commit-event",
        default="sys.soc_0.core_0.gpc.instruction_commit",
        help="Event name for committed instructions for first event stream",
    )
    parser.add_argument(
        "--es2-instr-commit-event",
        default="sys.soc_0.core_0.gpc.instruction_commit",
        help="Event name for committed instructions for second event stream",
    )
    parser.add_argument(
        "--es1-instr-commit-pc",
        default="sys.soc_0.core_0.gpc.commit_pc",
        help="Event data name for instruction commit PC for first event stream",
    )
    parser.add_argument(
        "--es2-instr-commit-pc",
        default="sys.soc_0.core_0.gpc.commit_pc",
        help="Event data name for instruction commit PC for second event stream",
    )
    parser.add_argument(
        "--es1-instr-commit-inum",
        default="sys.soc_0.core_0.gpc.inum",
        help="Event data name for instruction commit inum for first event stream",
    )
    parser.add_argument(
        "--es2-instr-commit-inum",
        default="sys.soc_0.core_0.gpc.inum",
        help="Event data name for instruction commit inum for second event stream",
    )
    parser.add_argument(
        "--max-instructions", type=int, default=30000, help="Stop checking after seeing this many committed instruction"
    )

    parser.add_argument(
        "--es1-inum-start",
        type=int,
        default=0,
        help="First instruction number (inum) to compare from event stream 1. Events prior to this will be ignored",
    )
    parser.add_argument(
        "--es2-inum-start",
        type=int,
        default=0,
        help="First instruction number (inum) to compare from event stream 2. Events prior to this will be ignored",
    )
    return parser.parse_args()


@functools.total_ordering
class CommittedInstruction:
    def __init__(self, inum, pc):
        self.inum = inum
        self.pc = pc

    def lower_pc(self):
        """
        Return the lower 48 bits of the PC. We don't compare the higher bits
        because they RTL and panthera eventstreams are inconsistent about
        including them.
        """
        return self.pc & ((2**48) - 1)

    def __lt__(self, other):
        if not isinstance(other, CommittedInstruction):
            return NotImplemented
        if self.inum < other.inum:
            return True
        elif self.inum > other.inum:
            return False
        return self.lower_pc() < other.lower_pc()

    def __eq__(self, other):
        if not isinstance(other, CommittedInstruction):
            return NotImplemented
        return self.inum == other.inum and self.lower_pc() == other.lower_pc()

    def __repr__(self):
        return f"CommittedInstruction(inum={self.inum}, PC=0x{self.pc:x})"


def event_stream_committed_instructions(es_file, commit_event, pc_event, inum_event, max_count, inum_start):
    reader = EventStreamReader(es_file, all_events=True)
    event = es_proto.Event()
    status = reader.read_event(event)
    count = 0

    # Find the definition for the instruction commit event
    committed_instr_defn = reader.find_definition_by_name(commit_event)
    committed_pc_defn = reader.find_definition_by_name(pc_event)
    committed_inum_defn = reader.find_definition_by_name(inum_event)

    if not committed_instr_defn:
        logging.error(f"Failed to find {commit_event} definition for {es_file}")
        sys.exit(1)
    if not committed_pc_defn:
        logging.error(f"Failed to find {pc_event} definition for {es_file}")
        sys.exit(1)
    if not committed_inum_defn:
        logging.error(f"Failed to find {inum_event} definition for {es_file}")
        sys.exit(1)

    while status and count < max_count:
        if event.definition_id == committed_instr_defn.id:
            pc = None
            inum = None
            for data in event.values:
                if data.definition_id == committed_pc_defn.id:
                    pc = data.uint_value
                elif data.definition_id == committed_inum_defn.id:
                    inum = getattr(data, data.WhichOneof("values"))

            if inum_start < inum:
                yield CommittedInstruction(inum, pc)
                count += 1

        event.Clear()
        status = reader.read_event(event)


def main(args):
    setup_logging()
    es1_instrs = event_stream_committed_instructions(
        args.es1,
        args.es1_instr_commit_event,
        args.es1_instr_commit_pc,
        args.es1_instr_commit_inum,
        args.max_instructions,
        args.es1_inum_start,
    )
    es2_instrs = event_stream_committed_instructions(
        args.es2,
        args.es2_instr_commit_event,
        args.es2_instr_commit_pc,
        args.es2_instr_commit_inum,
        args.max_instructions,
        args.es1_inum_start,
    )
    trace_name = os.path.basename(args.es1)
    instr_count = 0
    for es1_instr, es2_instr in zip_longest(es1_instrs, es2_instrs):
        instr_count += 1

        # if the instructions are None, it means that event stream yielded
        # fewer than the other (and fewer than our cutoff
        # args.max_instructions)
        if es1_instr is None:
            logging.info(f"Missing instruction {instr_count} in es1")
            sys.exit(1)
        if es2_instr is None:
            logging.info(f"Missing instruction {instr_count} in es2")
            sys.exit(1)

        if es1_instr != es2_instr:
            print(
                trace_name,
                f"Mismatched committed instructions. es1 {es1_instr} vs. es2 {es2_instr}",
            )
            sys.exit(1)

    if instr_count < args.max_instructions:
        print(
            trace_name,
            f"Error: Validated only {instr_count} instructions in the two event streams instead of the expected {args.max_instructions}",
        )
        sys.exit(1)

    print(
        trace_name,
        f"Successfully validated {instr_count} instructions in the two event streams",
    )


if __name__ == "__main__":
    main(parse_args())
