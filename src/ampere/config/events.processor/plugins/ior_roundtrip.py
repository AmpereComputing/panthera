# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import argparse
import re

import panthera.enums.chi_opcodes
from plugins.common.io_latency import IOLatency
from plugins.common.latency import Latency

parser = argparse.ArgumentParser()
parser.add_argument("--latency-ior", default=0, help="The ior that is running latency thread")
args = parser.parse_args(evp_args)
lat_ior = args.latency_ior


def inj_read_name(event_name):
    m = re.search("iorinjector_([0-9]+)", event_name)
    if m:
        return f"inj{m.group(1)}_read"
    else:
        return event_name


def inj_io_read_name(event_name):
    m = re.search("iorinjector_([0-9]+)", event_name)
    if m:
        return f"inj{m.group(1)}_io_read"
    else:
        return event_name


def inj_write_name(event_name):
    m = re.search("iorinjector_([0-9]+)", event_name)
    if m:
        return f"inj{m.group(1)}_write"
    else:
        return event_name


def inj_io_write_name(event_name):
    m = re.search("iorinjector_([0-9]+)", event_name)
    if m:
        return f"inj{m.group(1)}_io_write"
    else:
        return event_name


ReadOnce = panthera.enums.chi_opcodes.RequestOpcode["ReadOnce"].value
ReadNoSnp = panthera.enums.chi_opcodes.RequestOpcode["ReadNoSnp"].value
WriteNoSnpPtl = panthera.enums.chi_opcodes.RequestOpcode["WriteNoSnpPtl"].value
WriteUniquePtl = panthera.enums.chi_opcodes.RequestOpcode["WriteUniquePtl"].value


def is_read(e):
    return e.data["chi_req_opcode"] == ReadOnce or e.data["chi_req_opcode"] == ReadNoSnp


def is_io_read(e):
    return e.data["iotype"] == 0


def is_write(e):
    return e.data["chi_req_opcode"] == WriteNoSnpPtl or e.data["chi_req_opcode"] == WriteUniquePtl


def is_io_write(e):
    return e.data["iotype"] == 1


Latency(
    f"*.iorinjector_{lat_ior}.send_req",
    f"*.iorinjector_{lat_ior}.send_dat",
    "ior_writes_roundtrip_latency.csv",
    name_col="inj_latency",
    rename=inj_write_name,
    start_qual=is_write,
)
Latency(
    f"*.iorinjector_{lat_ior}.send_req",
    f"*.iorinjector_{lat_ior}.recv_dat",
    "ior_reads_roundtrip_latency.csv",
    name_col="inj_latency",
    rename=inj_read_name,
    start_qual=is_read,
)

IOLatency(
    f"*.iorinjector_{lat_ior}.alloc_io",
    f"*.iorinjector_{lat_ior}.wake",
    "ior_io_reads_alloc_to_wake_latency.csv",
    name_col="inj_latency",
    start_qual=is_io_read,
)
IOLatency(
    f"*.iorinjector_{lat_ior}.alloc_io",
    f"*.iorinjector_{lat_ior}.dealloc_io",
    "ior_io_writes_roundtrip_latency.csv",
    name_col="inj_latency",
    rename=inj_io_write_name,
    start_qual=is_io_write,
)
IOLatency(
    f"*.iorinjector_{lat_ior}.alloc_io",
    f"*.iorinjector_{lat_ior}.dealloc_io",
    "ior_io_reads_roundtrip_latency.csv",
    name_col="inj_latency",
    rename=inj_io_read_name,
    start_qual=is_io_read,
)
