#!/usr/bin/env python

# Copyright (c) 2018-2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import struct

SIZE_STRUCT = struct.Struct("<L")


def read32(_input):
    # Get message size in bytes, encoded as bytes
    num_bytes = _input.read(4)
    if len(num_bytes) != 4:
        return None
    return SIZE_STRUCT.unpack(num_bytes)[0]


def write32(value, output):
    # Write message size in bytes, encoded as bytes
    output.write(SIZE_STRUCT.pack(value))


def write_header_to(magic, version, output):
    write32(magic, output)
    write32(version, output)


def read_header_from(_input):
    magic = read32(_input)
    version = read32(_input)
    return (magic, version)


def write_delimited_to(msg, output):
    write32(msg.ByteSize(), output)
    output.write(msg.SerializeToString())


def read_delimited_from(_input, msg):
    if size := read32(_input):
        msg.ParseFromString(_input.read(size))
        return True
    else:
        return False
