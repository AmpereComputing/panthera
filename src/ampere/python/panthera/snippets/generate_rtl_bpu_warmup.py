#!/usr/bin/env python3
# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Generate RTL BPU warmup for RTL consumption.

Generate RTL BPU offline warmup collateral (a CSV file containing internal BPU
array state) from a BPU save/restore JSON file. This JSON file is expected to
have been previously generated with bobcat using a command something like:
`bobcat -top.core.bpu.save_warmup_file file.json.gz`
"""

import argparse
import csv
import gzip
import json
from dataclasses import astuple, dataclass
from enum import Enum


def mask(width):
    """Generate a bitmask `width` bits wide, starting at bit 0"""
    return (2**width) - 1


def check_width(val, width):
    """Raise an assertion if the value in `val` has bits set outside `width`
    bits wide (starting at bit 0)"""
    if mask(width) & val != val:
        raise AssertionError(f"0x{val:x} is wider than the expected {width} bits")


class BTBBranchType(Enum):
    """from ./rtl/Subsystems/Cpu/CpuPkg.sv:
    typedef enum logic [3:0] {BTB_TYPE_CONDITIONAL_DIRECT   = 4'b1000,
                              BTB_TYPE_UNCONDITIONAL_DIRECT = 4'b0011,
                              BTB_TYPE_CALL_DIRECT          = 4'b0010,
                              BTB_TYPE_CALL_INDIRECT        = 4'b0110,
                              BTB_TYPE_RETURN               = 4'b0101,
                              BTB_TYPE_INDIRECT             = 4'b0100,
                              BTB_TYPE_NOBRANCH             = 4'b0000,
                              BTB_TYPE_UNMAPPED_15          = 4'b1111,
                              BTB_TYPE_ILLEGAL              = 4'bxxxx
                             } BTB_AllBranchType_e;

    Note that there are aliases here, with the RTL versions first. This is to
    allow for creating instances of the enum values using the panthera string
    names, but so the RTL versions are preferred when converting to strings.
    """

    BTB_TYPE_CONDITIONAL_DIRECT = 0b1000  # rtl
    CONDITIONAL_DIRECT = 0b1000  # panthera
    BTB_TYPE_UNCONDITIONAL_DIRECT = 0b0011  # rtl
    UNCONDITIONAL_DIRECT = 0b0011  # panthera
    BTB_TYPE_CALL_DIRECT = 0b0010  # rtl
    CALL_DIRECT = 0b0010  # panthera
    BTB_TYPE_CALL_INDIRECT = 0b0110  # rtl
    CALL_INDIRECT = 0b0110  # panthera
    BTB_TYPE_RETURN = 0b0101  # rtl
    RETURN = 0b0101  # panthera
    BTB_TYPE_INDIRECT = 0b0100  # rtl
    UNCONDITIONAL_INDIRECT = 0b0100  # panthera
    BTB_TYPE_NOBRANCH = 0b0000  # rtl
    NONE = 0b0000  # panthera

    def __str__(self):
        return f"{self.name}"

    @staticmethod
    def is_direct(value):
        return value in [
            BTBBranchType.BTB_TYPE_CONDITIONAL_DIRECT,
            BTBBranchType.BTB_TYPE_UNCONDITIONAL_DIRECT,
            BTBBranchType.BTB_TYPE_CALL_DIRECT,
        ]


class BTBUpperPCSource(Enum):
    """from ./rtl/Subsystems/Cpu/Icf/Bpu/BpuPkg.sv:
    typedef enum logic [1:0] {BTB_SAME_UPPER_PC      = 2'b00,
                              BTB_INCREMENT_UPPER_PC = 2'b01,
                              BTB_DECREMENT_UPPER_PC = 2'b10,
                              BTB_OTHER_UPPER_PC     = 2'b11,
                              BTB_ILLEGAL_UPPER_PC   = 2'bxx
                             } BTB_UpperPCSource_e;
    """

    BTB_SAME_UPPER_PC = 0b00
    BTB_INCREMENT_UPPER_PC = 0b01
    BTB_DECREMENT_UPPER_PC = 0b10
    BTB_OTHER_UPPER_PC = 0b11

    def __str__(self):
        return f"{self.name}"

    @staticmethod
    def from_pc_diff(diff):
        diffmap = {
            -1: BTBUpperPCSource.BTB_DECREMENT_UPPER_PC,
            0: BTBUpperPCSource.BTB_SAME_UPPER_PC,
            1: BTBUpperPCSource.BTB_INCREMENT_UPPER_PC,
        }
        if diff in diffmap:
            return diffmap[diff]
        else:
            return BTBUpperPCSource.BTB_OTHER_UPPER_PC


@dataclass
class BTBEntry:
    tag: str
    el: str
    lower_target: str
    upper_pc_source: BTBUpperPCSource
    offset: str
    br_type: BTBBranchType


class Direction(Enum):
    """from ./rtl/Subsystems/Cpu/CpuPkg.sv:
    typedef enum logic {NOT_TAKEN = 1'b0, TAKEN = 1'b1, ILLEGAL_DIRECTION = 1'bx}  Direction_e;
    """

    NOT_TAKEN = 0
    TAKEN = 1

    def __str__(self):
        return f"{self.name}"


class TAGEStrength(Enum):
    """from ./rtl/Subsystems/Cpu/Icf/Bpu/BpuPkg.sv:
    typedef enum logic [1:0] {TAGE_VeryStrong = 2'd0, // 00 - Very strongly
                             TAGE_Strong     = 2'd1, // 01 - Strongly
                             TAGE_Weak       = 2'd2, // 10 - Weakly
                             TAGE_VeryWeak   = 2'd3, // 11 - Very Weakly
                             TAGE_Illegal    = 'x
                            } TAGE_Strength_e;

    Note that there are aliases here, with the RTL versions first. This is to
    allow for creating instances of the enum values using the panthera string
    names, but so the RTL versions are preferred when converting to strings.
    """

    TAGE_VeryStrong = 0  # rtl
    VERYSTRONG = 0  # panthera
    TAGE_Strong = 1  # rtl
    STRONG = 1  # panthera
    TAGE_Weak = 2  # rtl
    WEAK = 2  # panthera
    TAGE_VeryWeak = 3  # rtl
    VERYWEAK = 3  # panthera

    def __str__(self):
        return f"{self.name}"


@dataclass
class TAGEEntry:
    tag: str
    direction: Direction
    strength: TAGEStrength
    useful: str
    offset: str


class ITTAGEBranchType(Enum):
    """from ./rtl/Subsystems/Cpu/Icf/Bpu/BpuPkg.sv:
        typedef enum logic [1:0] {ITTAGE_INDIRECT = 2'd0, // 00
                                  ITTAGE_CALL     = 2'd1, // 01
                                  ITTAGE_RETURN   = 2'd2, // 10
                                  ITTAGE_NOBRANCH = 2'd3, // 11
                                  ITTAGE_ILLEGAL  = 'x
                                 } ITTAGE_Type_e;

    Note that there are aliases here, with the RTL versions first. This is to
    allow for creating instances of the enum values using the panthera string
    names, but so the RTL versions are preferred when converting to strings.
    """

    ITTAGE_INDIRECT = 0  # rtl
    UNCONDITIONAL_INDIRECT = 0  # panthera
    ITTAGE_CALL = 1  # rtl
    CALL_INDIRECT = 1  # panthera
    ITTAGE_RETURN = 2  # rtl
    RETURN = 2  # panthera
    ITTAGE_NOBRANCH = 3  # rtl
    NONE = 3  # panthera

    def __str__(self):
        return f"{self.name}"


class ITTAGEStrength(Enum):
    """from ./rtl/Subsystems/Cpu/Icf/Bpu/BpuPkg.sv:
    typedef enum logic [1:0] {ITTAGE_VeryStrong = 2'd0, // 00 - Very strongly
                             ITTAGE_Strong     = 2'd1, // 01 - Strongly
                             ITTAGE_Weak       = 2'd2, // 10 - Weakly
                             ITTAGE_VeryWeak   = 2'd3, // 11 - Very Weakly
                             ITTAGE_Illegal    = 'x
                            } ITTAGE_Strength_e;

    Note that there are aliases here, with the RTL versions first. This is to
    allow for creating instances of the enum values using the panthera string
    names, but so the RTL versions are preferred when converting to strings.
    """

    ITTAGE_VeryStrong = 0  # rtl
    VERYSTRONG = 0  # panthera
    ITTAGE_Strong = 1  # rtl
    STRONG = 1  # panthera
    ITTAGE_Weak = 2  # rtl
    WEAK = 2  # panthera
    ITTAGE_VeryWeak = 3  # rtl
    VERYWEAK = 3  # panthera

    def __str__(self):
        return f"{self.name}"


@dataclass
class ITTAGEEntry:
    tag: str
    el: str
    target: str
    offset: str
    br_type: ITTAGEBranchType
    strength: ITTAGEStrength
    useful: str


class TrinityBPU:
    def __init__(self, json, version):
        self.json = json

        self.version = version

        self.context_bits = 71

        # Expected BTB configuration
        self.btb_tag_bits = 10
        self.btb_sets = 2048
        self.btb_ways = 4

        # Expected Bimodal configuration
        self.bimodal_sets = 16384

        # Expected TAGE configuration
        self.tage_globals = 8
        self.tage_tag_bits = 8
        self.tage_sets = 1024
        self.tage_ways = 2

        # Expected ITTAGE configuration
        self.ittage_globals = 4
        self.ittage_tag_bits = 12
        self.ittage_sets = 256
        self.ittage_ways = 2

    def _gen_top_level_warmup(self, all_json, out):
        if self.version > 1:
            out.writerows([["===TOP==="], ["version", hex(self.version)]])

    def _gen_context_warmup(self, context_json, out):
        value = int(context_json["token"]["value"], 16)
        check_width(value, self.context_bits)
        out.writerows([["===CONTEXT==="]])
        if self.version > 1:
            out.writerows([["context_bits", hex(self.context_bits)]])
        out.writerows([[hex(value & mask(64))], [hex((value >> 64) & mask(64))]])

    def _gen_btb_entry(self, entry):
        """Generate an object representing a single BTB entry"""
        check_width(entry["tag"], self.btb_tag_bits)

        tag = hex(entry["tag"])
        el = f"EL{entry['el']}"
        target = int(entry["target"], 16)
        lower_target = hex((target >> 2) & mask(26))
        offset = hex(entry["offset"])
        brtype = BTBBranchType[entry["branch_type"]]

        # calculate upper PC source from full target and full branch PC
        br_pc = int(entry["branch_pc"], 16)
        # Bits below 28 are considered 'LowerPC' (of the target) in RTL
        upper_br_pc = br_pc >> 28
        upper_target = target >> 28
        upper_pc_source = BTBUpperPCSource.from_pc_diff(upper_br_pc - upper_target)

        return BTBEntry(tag, el, lower_target, upper_pc_source, offset, brtype)

    def _gen_btb_set(self, btb_set, out):
        """Generate warmup CSV for one set in the BTB, ensuring there are no
        duplicate tags. If there are duplicate tags for invalid ways, increment
        their tags until there is no duplication."""
        assert len(btb_set) == self.btb_ways, (
            f"Found BTB set with {len(btb_set)} ways instead of expected {self.btb_ways}"
        )
        unique_tags = set()
        ways = []
        for way in btb_set:
            entry = self._gen_btb_entry(way)
            if entry.br_type != BTBBranchType.BTB_TYPE_NOBRANCH:
                full_tag = (entry.tag, entry.el, entry.offset)
                assert full_tag not in unique_tags
                unique_tags.add(full_tag)
            ways.append(entry)

        for entry in ways:
            full_tag = (entry.tag, entry.el, entry.offset)
            if entry.br_type == BTBBranchType.BTB_TYPE_NOBRANCH:
                while full_tag in unique_tags:
                    entry.tag = hex(int(entry.tag, 16) + 1)
                    full_tag = (entry.tag, entry.el, entry.offset)
                unique_tags.add(full_tag)
        out.writerows([astuple(way) for way in ways])

    def _gen_btb_warmup(self, btb_json, out):
        """Generate warmup CSV for the whole BTB"""
        out.writerow(["===BTB==="])
        assert btb_json["num_sets"] == self.btb_sets, (
            f"Number of BTB sets in input JSON, {btb_json['num_sets']}, does not match expected {self.btb_sets}"
        )
        assert btb_json["num_ways"] == self.btb_ways, (
            f"Number of BTB ways in input JSON, {btb_json['num_ways']}, does not match expected {self.btb_ways}"
        )
        assert btb_json["tag_bits"] == self.btb_tag_bits, (
            f"Number of BTB tag bits in input JSON, {btb_json['tag_bits']}, does not match expected {self.btb_tag_bits}"
        )

        if self.version > 1:
            out.writerows(
                [
                    ["btb_sets", hex(self.btb_sets)],
                    ["btb_ways", hex(self.btb_ways)],
                    ["btb_tag_bits", hex(self.btb_tag_bits)],
                    ["btb_el_tagged", hex(btb_json["el_tagged"])],
                ]
            )

        for btb_set in btb_json["sets"]:
            self._gen_btb_set(btb_set, out)

    def _gen_tage_entry(self, entry):
        """Generate object for a single TAGE entry"""
        check_width(entry["tag"], self.tage_tag_bits)

        tag = hex(entry["tag"])
        direction = Direction.TAKEN if entry["taken"] else Direction.NOT_TAKEN
        strength = TAGEStrength[entry["strength"]]
        useful = "0x1" if entry["useful"] else "0x0"
        offset = hex(entry["offset"])

        return TAGEEntry(tag, direction, strength, useful, offset)

    def _gen_tage_set(self, tage_set, out):
        """Generate warmup CSV for a single TAGE set, ensuring there are no
        duplicate tags"""
        assert len(tage_set) == self.tage_ways, (
            f"Found TAGE set with {len(tage_set)} ways instead of expected {self.tage_ways}"
        )
        unique_tags = set()
        ways = []
        for way in tage_set:
            entry = self._gen_tage_entry(way)
            full_tag = (entry.tag, entry.offset)
            assert full_tag not in unique_tags
            unique_tags.add(full_tag)
            ways.append(entry)
        out.writerows([astuple(way) for way in ways])

    def _gen_tage_warmup(self, tage_json, out):
        """Generate warmup for all TAGE arrays (in order)"""
        out.writerow(["===TAGE==="])
        assert tage_json["num_globals"] == self.tage_globals, (
            f"Number of TAGE global arrays in input JSON, {tage_json['num_globals']}, does not match expected {self.tage_globals}"
        )
        assert tage_json["tag_bits"] == self.tage_tag_bits, (
            f"Number of TAGE tag bits in input JSON, {tage_json['tag_bits']}, does not match expected {self.tage_tag_bits}"
        )
        assert tage_json["num_sets"] == self.tage_sets, (
            f"Number of TAGE sets in input JSON, {tage_json['num_sets']}, does not match expected {self.tage_sets}"
        )
        assert tage_json["num_ways"] == self.tage_ways, (
            f"Number of TAGE ways in input JSON, {tage_json['num_ways']}, does not match expected {self.tage_ways}"
        )

        if self.version > 1:
            out.writerows(
                [
                    ["tage_globals", hex(self.tage_globals)],
                    ["tage_global_sets", hex(self.tage_sets)],
                    ["tage_global_ways", hex(self.tage_ways)],
                    ["tage_tag_bits", hex(self.tage_tag_bits)],
                ]
            )

        for array in tage_json["arrays"]:
            for tage_set in array:
                self._gen_tage_set(tage_set, out)

    def _gen_ittage_entry(self, entry):
        """Generate object for a single ITTAGE entry"""
        check_width(entry["tag"], self.ittage_tag_bits)

        tag = hex(entry["tag"])
        el = f"EL{entry['el']}"
        target = int(entry["target"], 16)
        target = hex((target >> 2) & mask(53 - 2))
        offset = hex(entry["offset"])
        brtype = ITTAGEBranchType[entry["branch_type"]]
        strength = ITTAGEStrength[entry["strength"]]
        useful = "0x1" if entry["useful"] else "0x0"

        return ITTAGEEntry(tag, el, target, offset, brtype, strength, useful)

    def _gen_ittage_set(self, ittage_set, out):
        """Generate warmup CSV for a single ITTAGE set, ensuring there are no
        duplicate tags"""
        assert len(ittage_set) == self.ittage_ways, (
            f"Found ITTAGE set with {len(ittage_set)} ways instead of expected {self.ittage_ways}"
        )
        unique_tags = set()
        ways = []
        for way in ittage_set:
            entry = self._gen_ittage_entry(way)
            full_tag = (entry.tag, entry.el, entry.offset)
            assert full_tag not in unique_tags
            unique_tags.add(full_tag)
            ways.append(entry)
        out.writerows([astuple(way) for way in ways])

    def _gen_ittage_warmup(self, ittage_json, out):
        """Generate warmup for all ITTAGE arrays (in order)"""
        out.writerow(["===ITTAGE==="])
        assert ittage_json["num_globals"] == self.ittage_globals, (
            f"Number of ITTAGE global arrays in input JSON, {ittage_json['num_globals']}, does not match expected {self.ittage_globals}"
        )
        assert ittage_json["tag_bits"] == self.ittage_tag_bits, (
            f"Number of ITTAGE tag bits in input JSON, {ittage_json['tag_bits']}, does not match expected {self.ittage_tag_bits}"
        )
        assert ittage_json["num_sets"] == self.ittage_sets, (
            f"Number of ITTAGE sets in input JSON, {ittage_json['num_sets']}, does not match expected {self.ittage_sets}"
        )
        assert ittage_json["num_ways"] == self.ittage_ways, (
            f"Number of ITTAGE ways in input JSON, {ittage_json['num_ways']}, does not match expected {self.ittage_ways}"
        )

        if self.version > 1:
            out.writerows(
                [
                    ["ittage_globals", hex(self.ittage_globals)],
                    ["ittage_global_sets", hex(self.ittage_sets)],
                    ["ittage_global_ways", hex(self.ittage_ways)],
                    ["ittage_tag_bits", hex(self.ittage_tag_bits)],
                ]
            )

        for array in ittage_json["arrays"]:
            for ittage_set in array:
                self._gen_ittage_set(ittage_set, out)

    def _gen_bimodal_set(self, bimodal_set, out):
        """Generate warmup bitfield for a single Bimodal entry's direction or
        hysteresis bits"""
        bitfield = 0
        for offset in range(len(bimodal_set)):
            if bimodal_set[offset] == 1:
                bitfield |= 1 << offset
        out.writerow([hex(bitfield)])

    def _gen_bimodal_direction(self, bimodal_json, out):
        """Generate warmup for Bimodal array direction bits (in order)"""
        out.writerow(["===TBP==="])
        assert bimodal_json["num_sets"] == self.bimodal_sets, (
            f"Number of Bimodal sets in input JSON, {bimodal_json['num_sets']}, does not match expected {self.bimodal_sets}"
        )

        if self.version > 1:
            out.writerows(
                [
                    ["bimodal_sets", hex(self.bimodal_sets)],
                    ["bimodal_direction_bits", hex(16)],
                ]
            )

        for bimodal_set in bimodal_json["sets"]:
            assert len(bimodal_set["direction"]) == 16
            self._gen_bimodal_set(bimodal_set["direction"], out)

    def _gen_bimodal_hysteresis(self, bimodal_json, out):
        """Generate warmup for Bimodal array hysteresis (strength) bits (in order)"""
        out.writerow(["===TBH==="])
        assert bimodal_json["num_sets"] == self.bimodal_sets, (
            f"Number of Bimodal sets in input JSON, {bimodal_json['num_sets']}, does not match expected {self.bimodal_sets}"
        )

        if self.version > 1:
            out.writerows(
                [
                    ["bimodal_sets", hex(self.bimodal_sets)],
                    ["bimodal_hysteresis_bits", hex(4)],
                ]
            )

        for bimodal_set in bimodal_json["sets"]:
            assert len(bimodal_set["hysteresis"]) == 4
            self._gen_bimodal_set(bimodal_set["hysteresis"], out)

    def gen_rtl_warmup(self, output_file):
        self._gen_top_level_warmup(self.json, output_file)
        self._gen_context_warmup(self.json["context"], output_file)
        self._gen_btb_warmup(self.json["L2BTB"], output_file)
        self._gen_tage_warmup(self.json["TAGE"], output_file)
        self._gen_ittage_warmup(self.json["ITTAGE"], output_file)
        self._gen_bimodal_direction(self.json["bimodals"], output_file)
        self._gen_bimodal_hysteresis(self.json["bimodals"], output_file)


class ConnorBPU(TrinityBPU):
    def __init__(self, json, version):
        super().__init__(json, version)

        # Add 2 additional ITTAGE global arrays beyond Trinity's 4
        self.ittage_globals = 6

        # BTB tag bits reduced to 8
        self.btb_tag_bits = 8

        # Increase the width of the BPU context token
        self.context_bits = 128


class ConnorAIBPU(ConnorBPU):
    def __init__(self, json, version):
        super().__init__(json, version)

        # Shrink number of sets for BTB, Bimodals, TAGE and ITTAGE globals
        self.btb_sets = 128
        self.btb_ways = 8
        self.bimodal_sets = 256
        self.tage_sets = 64
        self.ittage_sets = 4


cores = {
    "trinity": TrinityBPU,
    "connor": ConnorBPU,
    "connor-ai": ConnorAIBPU,
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", choices=cores.keys(), default="connor", help="Core for which to generate warmup")
    parser.add_argument(
        "--panthera-warmup",
        required=True,
        help="Panthera BPU warmup file (generated with bobcat -top.core.bpu.save_warmup_file {name}.bpu_warmup.json.gz)",
    )
    parser.add_argument("--rtl-warmup-file", required=True, help="Path at which to store RTL BPU warmup CSV file")
    parser.add_argument("--rtl-csv-version", default=1, type=int, choices=[1, 2], help="Version of CSV file to output")
    args = parser.parse_args()

    if args.panthera_warmup.endswith(".gz"):
        input_json_file = gzip.open(args.panthera_warmup, "rb")
    else:
        input_json_file = open(args.panthera_warmup, "r")

    input_json = json.load(input_json_file)

    bpu = cores[args.core](input_json, args.rtl_csv_version)

    with open(args.rtl_warmup_file, "w") as output_file:
        csv_file = csv.writer(output_file)
        bpu.gen_rtl_warmup(csv_file)


if __name__ == "__main__":
    main()
