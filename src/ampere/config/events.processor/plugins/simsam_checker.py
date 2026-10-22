# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import argparse
import re
import subprocess
import sys
from collections import defaultdict

import evp
from panthera.enums.chi_opcodes import DataOpcode, RequestOpcode, ResponseOpcode, SnoopOpcode
from plugins.common.transaction_tracker import TransactionTracker


class SimSamChecker:
    def __init__(self, args):
        super().__init__()
        self.simsam_bin = args.simsam
        self.simsam_args = args.simsam_args
        self.sf = args.sf
        self.addrs = defaultdict(lambda: {})

        evp.on("*.core_#.send_req", lambda e: self.core_check_addr(e))
        evp.on("*.pxha_#.send_req", lambda e: self.pxha_check_addr(e))
        evp.on("*.pmha_#.send_req", lambda e: self.pmha_check_addr(e))
        if self.sf:
            evp.on("*.pxha_#.snoop_filter.access", lambda e: self.pxha_sf_check_addr(e))
            evp.on("*.pmha_#.snoop_filter.access", lambda e: self.pmha_sf_check_addr(e))

    def _exec_simsam(self, addr):
        cmd = [self.simsam_bin] + self.simsam_args + ["--start", f"0x{addr:x}"]
        result = subprocess.run(cmd, stdout=subprocess.PIPE, text=True, check=True)

        # Format: 0x000000013880 P=0x000000014c80 DDR PxHA=0.81 PxHA SF bank=2 PxHA SF set=19 PmHA=0.105  PmHA SF bank=2 PmHA SF set=19 MCU=0.160 MCU addr=0x000000000271
        regex = r"\n([^ ]+) P=([^ ]+) ([^ ]+) PxHA=(\d+.\d+) PxHA SF bank=(\d+) PxHA SF set=(\d+) PmHA=(\d+.\d+).*PmHA SF bank=(\d+) PmHA SF set=(\d+) MCU=(\d+.\d+) MCU addr=([^ ]+)\n"
        match = re.match(regex, result.stdout)

        if match:
            h = {}
            h["Address"] = int(match.group(1), 16)
            h["Permuted Address"] = int(match.group(2), 16)
            h["Region Type"] = match.group(3)
            h["PxHA Id"] = match.group(4)
            h["PxHA SF Bank"] = int(match.group(5))
            h["PxHA SF Set"] = int(match.group(6))
            h["PmHA Id"] = match.group(7)
            h["PmHA SF Bank"] = int(match.group(8))
            h["PmHA SF Set"] = int(match.group(9))
            h["Mcu Id"] = match.group(10)
            h["Mcu local Address"] = int(match.group(11), 16)
            return h
        else:
            raise Exception(f"Failed to match string '{result.stdout}'.")

    def elements_complete(self, address):
        if address in self.addrs:
            h = self.addrs[address]
            keys = ["Address", "PxHA Id", "PmHA Id", "Mcu Id"]
            if self.sf:
                keys += ["PxHA SF Bank", "PxHA SF Set", "PmHA SF Bank", "PmHA SF Set"]
            return all(key in h for key in keys)
        return False

    def check_simsam(self, address):
        if not self.elements_complete(address):
            return

        h = self._exec_simsam(address)
        e = self.addrs[address]

        for key in e:
            if e[key] != h[key]:
                print(
                    f"Addr: 0x{address:x} Event stream Hash Element'{key}' '{e[key]}' does not match simsam output '{h[key]}'"
                )
        # del self.addrs[address]

    def core_check_addr(self, event):
        opcode = RequestOpcode(event.data["chi_req_opcode"])
        if opcode == RequestOpcode.ReadShared:
            addr = event.data["physical_address"]
            self.addrs[addr]["Address"] = event.data["physical_address"]
            self.addrs[addr]["PxHA Id"] = f"0.{event.data['tgtid']}"
            self.check_simsam(addr)

    def pxha_sf_check_addr(self, event):
        addr = event.data["physical_address"]
        self.addrs[addr]["PxHA SF Bank"] = event.data["bank"]
        self.addrs[addr]["PxHA SF Set"] = event.data["set"]
        self.check_simsam(addr)

    def pxha_check_addr(self, event):
        opcode = RequestOpcode(event.data["chi_req_opcode"])
        if opcode == RequestOpcode.ReadShared:
            addr = event.data["physical_address"]
            self.addrs[addr]["PmHA Id"] = f"0.{event.data['tgtid']}"
            self.check_simsam(addr)

    def pmha_check_addr(self, event):
        opcode = RequestOpcode(event.data["chi_req_opcode"])
        if opcode == RequestOpcode.ReadNoSnpSep:
            addr = event.data["physical_address"]
            self.addrs[addr]["Mcu Id"] = f"0.{event.data['tgtid']}"
            self.check_simsam(addr)

    def pmha_sf_check_addr(self, event):
        addr = event.data["physical_address"]
        self.addrs[addr]["PmHA SF Bank"] = event.data["bank"]
        self.addrs[addr]["PmHA SF Set"] = event.data["set"]
        self.check_simsam(addr)


parser = argparse.ArgumentParser()
parser.add_argument("--simsam", type=str, required=True, help="Path to simsam binary")
parser.add_argument("--simsam-args", type=str, nargs="+", help="array of arguments to pass config files to simsam")
parser.add_argument("--sf", action="store_true", help="Verify SnoopFilter Access (panthera only)")
args = parser.parse_args(evp_args)

SimSamChecker(args)
