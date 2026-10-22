# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import argparse
import enum

import evp

expand_enumerations = False
equal = None
expect_enumerations = []


def print_event(event):
    global expand_enumerations
    global equal
    global expect_enumerations

    def data_type(ed):
        if isinstance(ed, enum.Enum):
            return "E"
        return type(ed).__name__[0]

    if expect_enumerations:
        for name in expect_enumerations:
            if name not in evp.enums:
                raise ValueError(f"{name} not in evp.enums")

    if expand_enumerations:
        data = ", ".join(f"{k}({data_type(v)})={str(v)}" for k, v in event.data.items())
    else:
        data = ", ".join(f"{k}({data_type(v)})={int(v)}" for k, v in event.data.items())

    print(f"{event.time} {event.name} {data}".strip())
    if equal is not None:
        for k, v in event.data.items():
            v = str(v) if expand_enumerations else v
            if v == equal:
                print(f"equal: {k}={v} == {equal}")


parser = argparse.ArgumentParser()
parser.add_argument("event")
parser.add_argument("--eq")
parser.add_argument("--expect-enum", action="append")
parser.add_argument("--expand-enums", action="store_true")
args = parser.parse_args(evp_args)

expand_enumerations = args.expand_enums
expect_enumerations = args.expect_enum
if args.eq:
    try:
        equal = int(args.eq)
    except ValueError:
        equal = str(args.eq)

evp.on(args.event, print_event)
