# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import argparse
import os

import evp
from panthera.enums.chi_opcodes import ResponseOpcode
from plugins.common.latency import Latency
from plugins.common.latency_breakdown import LatencyBreakdown

parser = argparse.ArgumentParser()
parser.add_argument("--latency-core", default=0, help="The core that is running latency thread")
parser.add_argument("--report-rdi", action="store_true", help="Display average latencies of rdi ports")
parser.add_argument(
    "--name", default="lat_breakdown_summary.csv", help="CSV output filename, default = lat_breakdown_summary.csv"
)
args = parser.parse_args(evp_args)
lat_core = args.latency_core
report_rdi = args.report_rdi
out_filename = args.name


events = [
    f"*.core_{lat_core}.send_req",
    f"*.core_{lat_core}.recv_dat",
    (f"*.core_{lat_core}.recv_rsp", lambda event: True if event.data["chi_rsp_opcode"] == 3 else False),
    "*.pxha_#.recv_req",
    "*.pxha_#.send_req",
    "*.pmha_#.recv_req",
    "*.pmha_#.send_req",
    "*.memory_#.recv_req",
    "*.memory_#.send_dat",
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
    ("core_to_pxha", (r".*\.core_\d+\.send_req", r".*pxha_\d+\.recv_req")),
    ("in_pxha", (r".*pxha_\d+\.recv_req", r".*pxha_\d+\.send_req")),
    ("pxha_to_pmha", (r".*pxha_\d+\.send_req", r".*pmha_\d+\.recv_req")),
    ("in_pmha", (r".*pmha_\d+\.recv_req", r".*pmha_\d+\.send_req")),
    ("pmha_to_mcu", (r".*pmha_\d+\.send_req", r".*memory_\d+\.recv_req")),
    ("in_mcu", (r".*memory_\d+\.recv_req", r".*memory_\d+\.send_dat")),
    ("mcu_to_core", (r".*memory_\d+\.send_dat", r".*\.core_\d+\.recv_dat")),
    ("pxha_to_rdi", (r".*pxha_\d+\.send_req", r".*rdi.*\.recv_req")),
    ("in_rdi", (r".*rdi.*\.recv_req", r".*rdi.*\.send_req")),
    ("in_rdi", (r".*rdi.*\.recv_dat", r".*rdi.*\.send_dat")),
    ("rdi_to_pmha", (r".*rdi.*\.send_req", r".*pmha_\d+\.recv_req")),
    ("mcu_to_rdi", (r".*memory_\d+\.send_dat", r".*rdi.*\.recv_dat")),
    ("rdi_to_core", (r".*rdi.*\.send_dat", r".*\.core_\d+\.recv_dat")),
    ("rdi_to_rdi", (r".*rdi.*\.send_dat", r".*rdi.*\.recv_dat")),
    ("rdi_to_rdi", (r".*rdi.*\.send_req", r".*rdi.*\.recv_req")),
    ("in_core_retry", (r".*\.core_\d+\.recv_rsp", r".*\.core_\d+\.send_req")),
    ("pxha_to_core_retry", (r".*pxha_\d+\.send_rsp", r".*\.core_\d+\.recv_rsp")),
]

LatencyBreakdown(
    events, f"sys.soc_0.core_{lat_core}.send_req", f"sys.soc_0.core_{lat_core}.recv_dat", rename_rules, out_filename
)
