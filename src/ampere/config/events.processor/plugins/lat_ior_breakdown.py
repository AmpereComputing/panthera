# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import argparse
import os

import evp
from panthera.enums.chi_opcodes import ResponseOpcode
from plugins.common.latency import Latency
from plugins.common.latency_breakdown import LatencyBreakdown

parser = argparse.ArgumentParser()
parser.add_argument("--latency-ior", default=0, help="The ior that is running latency thread")
parser.add_argument("--report-rdi", action="store_true", help="Display average latencies of rdi ports")
parser.add_argument(
    "--name", default="lat_breakdown_summary.csv", help="CSV output filename, default = lat_breakdown_summary.csv"
)
args = parser.parse_args(evp_args)
lat_ior = args.latency_ior
report_rdi = args.report_rdi
out_filename = args.name


events = [
    f"*.iorinjector_{lat_ior}.send_req",
    f"*.iorinjector_{lat_ior}.recv_dat",
    (f"*.iorinjector_{lat_ior}.recv_rsp", lambda event: True if event.data["chi_rsp_opcode"] == 5 else False),
    "*.pmha_#.recv_req",
    "*.pmha_#.send_req",
    "*.memory_#.chi_interface.recv_req",
    "*.memory_#.chi_interface.send_dat",
]

if report_rdi:
    events += [
        "*.rdi_die#_X#_Y#_P#_to_die#_X#_Y#_P#.a_to_b.send_req",
        "*.rdi_die#_X#_Y#_P#_to_die#_X#_Y#_P#.b_to_a.send_req",
        "*.rdi_die#_X#_Y#_P#_to_die#_X#_Y#_P#.a_to_b.recv_req",
        "*.rdi_die#_X#_Y#_P#_to_die#_X#_Y#_P#.b_to_a.recv_req",
        "*.rdi_die#_X#_Y#_P#_to_die#_X#_Y#_P#.a_to_b.send_dat",
        "*.rdi_die#_X#_Y#_P#_to_die#_X#_Y#_P#.b_to_a.send_dat",
        "*.rdi_die#_X#_Y#_P#_to_die#_X#_Y#_P#.a_to_b.recv_dat",
        "*.rdi_die#_X#_Y#_P#_to_die#_X#_Y#_P#.b_to_a.recv_dat",
    ]

# Category,  Src CHI Port, Tgt CHI Port
rename_rules = [
    ("ior_to_rdi", (r".*\.iorinjector_\d+\.send_req", r".*rdi.*\.recv_req")),
    ("in_rdi", (r".*rdi.*\.recv_req", r".*rdi.*\.send_req")),
    ("rdi_to_rdi", (r".*rdi.*\.send_req", r".*rdi.*\.recv_req")),
    ("rdi_to_pmha", (r".*rdi.*\.send_req", r".*pmha_\d+\.recv_req")),
    ("in_pmha", (r".*pmha_\d+\.recv_req", r".*pmha_\d+\.send_req")),
    ("pmha_to_mcu", (r".*pmha_\d+\.send_req", r".*memory.*\.recv_req")),
    ("in_mcu", (r".*memory.*\.recv_req", r".*memory.*\.send_dat")),
    ("mcu_to_rdi", (r".*memory.*\.send_dat", r".*rdi.*\.recv_dat")),
    ("in_rdi", (r".*rdi.*\.recv_dat", r".*rdi.*\.send_dat")),
    ("rdi_to_rdi", (r".*rdi.*\.send_dat", r".*rdi.*\.recv_dat")),
    ("rdi_to_ior", (r".*rdi.*\.send_dat", r".*\.iorinjector_\d+\.recv_dat")),
]

LatencyBreakdown(
    events,
    f"sys.soc_0.iorinjector_{lat_ior}.send_req",
    f"sys.soc_0.iorinjector_{lat_ior}.recv_dat",
    rename_rules,
    out_filename,
)
