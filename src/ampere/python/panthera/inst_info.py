# Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from __future__ import annotations

import argparse
import collections
import re
import sys

from ampere.xlat.pattern import Pattern
from ampere.xlat.translation import Fusion, translations

INST_INFO_CC = """//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
//

#include "ampere/archx/inst_info.h"

namespace ampere::archx {{

InstInfo inst_info_tab[] = {{
    {inst_info_entries}
}};

}} // namespace ampere::archx

"""


def main():
    args = parse_args()

    xlat_globals = {}

    for input_file in args.inputs:
        exec(compile(open(input_file).read(), input_file, mode="exec"), xlat_globals)

    inst_info_buckets = build_inst_info()
    inst_info_entries = buckets_to_entries(list(inst_info_buckets.keys()))
    with open(f"{args.cpu}_inst_info.cc", "w") as inst_info_cc:
        inst_info_cc.write(INST_INFO_CC.format(inst_info_entries=inst_info_entries))
    with open(f"{args.cpu}inst_info.patterns", "w") as inst_info_patterns:
        for patterns in inst_info_buckets.values():
            print("|".join(patterns), file=inst_info_patterns)


def parse_args():
    parser = argparse.ArgumentParser(description="Build instruction info tables for a given CPU")
    parser.add_argument("--cpu", help="CPU name", required=True)
    parser.add_argument("inputs", nargs="+", help="XLAT input files")
    return parser.parse_args()


def _make_pattern():
    return Pattern(32)


class BranchDetector:
    """Determine a branch type based on the instruction opcode."""

    CONDITIONAL_DIRECT = [
        "0101010-------------------------",
        "-011010-------------------------",
        "-011011-------------------------",
    ]

    UNCONDITIONAL_DIRECT = ["000101--------------------------"]

    UNCONDITIONAL_INDIRECT = ["11010110000---------------------", "11010111000---------------------"]

    CALL_DIRECT = ["100101--------------------------"]

    CALL_INDIRECT = ["11010110001---------------------"]

    RETURN = ["11010110010---------------------"]

    def __init__(self):
        self.p_conditional_direct = self.build_pattern(self.CONDITIONAL_DIRECT)
        self.p_unconditional_direct = self.build_pattern(self.UNCONDITIONAL_DIRECT)
        self.p_unconditional_indirect = self.build_pattern(self.UNCONDITIONAL_INDIRECT)
        self.p_call_direct = self.build_pattern(self.CALL_DIRECT)
        self.p_call_indirect = self.build_pattern(self.CALL_INDIRECT)
        self.p_return = self.build_pattern(self.RETURN)

    @staticmethod
    def build_pattern(patterns):
        pat = _make_pattern()
        for p in patterns:
            pat.add_pattern(p)
        return pat

    def branch_type(self, pattern):
        test_pattern = _make_pattern()
        test_pattern.add_pattern(pattern)

        if self.p_conditional_direct.p_intersects(test_pattern):
            return "BranchType::CONDITIONAL_DIRECT"
        elif self.p_unconditional_direct.p_intersects(test_pattern):
            return "BranchType::UNCONDITIONAL_DIRECT"
        elif self.p_unconditional_indirect.p_intersects(test_pattern):
            return "BranchType::UNCONDITIONAL_INDIRECT"
        elif self.p_call_direct.p_intersects(test_pattern):
            return "BranchType::CALL_DIRECT"
        elif self.p_call_indirect.p_intersects(test_pattern):
            return "BranchType::CALL_INDIRECT"
        elif self.p_return.p_intersects(test_pattern):
            return "BranchType::RETURN"
        else:
            return "BranchType::NONE"


class BarrierDetector:
    DSB = ["11010101000000110011----10011111"]
    DMB = ["11010101000000110011----10111111"]
    ISB = ["11010101000000110011----11011111"]
    CSDB = ["11010101000000110010001010011111"]

    def __init__(self):
        self.dsb = self.build_pattern(self.DSB)
        self.dmb = self.build_pattern(self.DMB)
        self.isb = self.build_pattern(self.ISB)
        self.csdb = self.build_pattern(self.CSDB)

    @staticmethod
    def build_pattern(patterns):
        pat = _make_pattern()
        for p in patterns:
            pat.add_pattern(p)
        return pat

    def barrier_type(self, pattern):
        test_pattern = _make_pattern()
        test_pattern.add_pattern(pattern)

        if self.dsb.p_intersects(test_pattern):
            return "Barrier_Type::DSB"
        elif self.dmb.p_intersects(test_pattern):
            return "Barrier_Type::DMB"
        elif self.isb.p_intersects(test_pattern):
            return "Barrier_Type::ISB"
        elif self.csdb.p_intersects(test_pattern):
            return "Barrier_Type::CSDB"
        else:
            return "Barrier_Type::NONE"


class MoveDetector:
    #
    # Detect the four instruction classes which might produce a register move or move
    # of zero, which might get eliminated in IDR:
    #
    # class ORR_64_log_shift
    #     pfields = 'sf opc       shift N  Rm    imm6   Rn    Rd  '
    #     pattern = '1  01  01010 --    0 ----- ------ ----- -----'
    #
    # class ADD_64_addsub_imm
    #     pfields = 'sf op S        sh    imm12      Rn    Rd  '
    #     pattern = '1  0  0 100010 -  ------------ ----- -----'
    #
    # class ORR_32_log_shift
    #     pfields = 'sf opc       shift N  Rm    imm6   Rn    Rd  '
    #     pattern = '0  01  01010 --    0 ----- ------ ----- -----'
    #
    # class MOVZ_32_movewide
    #     pfields = 'sf opc        hw      imm16        Rd  '
    #     pattern = '0  10  100101 0- ---------------- -----'
    #
    # class MOVZ_64_movewide
    #     pfields = 'sf opc        hw      imm16        Rd  '
    #     pattern = '1  10  100101 -- ---------------- -----'

    ORR64 = ["10101010--0---------------------"]
    ADD64 = ["100100010-----------------------"]
    ORR32 = ["00101010--0---------------------"]
    MOVZ = ["0101001010----------------------", "110100101-----------------------"]

    def __init__(self):
        self.orr64 = self.build_pattern(self.ORR64)
        self.add64 = self.build_pattern(self.ADD64)
        self.orr32 = self.build_pattern(self.ORR32)
        self.movz = self.build_pattern(self.MOVZ)

    @staticmethod
    def build_pattern(patterns):
        pat = _make_pattern()
        for p in patterns:
            pat.add_pattern(p)
        return pat

    def move_type(self, pattern):
        test_pattern = _make_pattern()
        test_pattern.add_pattern(pattern)

        if self.orr64.p_intersects(test_pattern):
            return "MoveType::ORR64"
        elif self.add64.p_intersects(test_pattern):
            return "MoveType::ADD64"
        elif self.orr32.p_intersects(test_pattern):
            return "MoveType::ORR32"
        elif self.movz.p_intersects(test_pattern):
            return "MoveType::MOVZ"
        else:
            return "MoveType::NONE"


class DivideSources:
    """integer divide

        pfields = 'sf op S           Rm         o1  Rn    Rd  '
        pattern = '0  0  0 11010110 ----- 00001 1  ----- -----'
        pattern = '1  0  0 11010110 ----- 00001 1  ----- -----'
        pattern = '0  0  0 11010110 ----- 00001 0  ----- -----'
        pattern = '1  0  0 11010110 ----- 00001 0  ----- -----'

    FP scalar divide

        pfields = 'M   S       ftype    Rm   opcode     Rn    Rd  '
        pattern = '0 0 0 11110 11    1 ----- 0001   10 ----- -----'
        pattern = '0 0 0 11110 00    1 ----- 0001   10 ----- -----'
        pattern = '0 0 0 11110 01    1 ----- 0001   10 ----- -----'

    FP scalar sqrt

        pfields = 'M   S       ftype        opc        Rn    Rd  '
        pattern = '0 0 0 11110 11    1 0000 11  10000 ----- -----'
        pattern = '0 0 0 11110 00    1 0000 11  10000 ----- -----'
        pattern = '0 0 0 11110 01    1 0000 11  10000 ----- -----'

    FP vector divide

        pfields = '  Q U       a     Rm      opcode    Rn    Rd  '
        pattern = '0 - 1 01110 0 10 ----- 00 111    1 ----- -----'

        pfields = '  Q U         sz    Rm   opcode    Rn    Rd  '
        pattern = '0 - 1 01110 0 -  1 ----- 11111  1 ----- -----'

    FP vector sqrt

        pfields = '  Q U       a        opcode     Rn    Rd  '
        pattern = '0 - 1 01110 1 111100 11111  10 ----- -----'

        pfields = '  Q U         sz       opcode     Rn    Rd  '
        pattern = '0 - 1 01110 1 -  10000 11111  10 ----- -----'
    """

    int_div = [
        "00011010110-----000011----------",
        "10011010110-----000011----------",
        "00011010110-----000010----------",
        "10011010110-----000010----------",
    ]

    fp_scalar_div = [
        "00011110111-----000110----------",
        "00011110001-----000110----------",
        "00011110011-----000110----------",
    ]
    fp_scalar_sqrt = [
        "0001111011100001110000----------",
        "0001111000100001110000----------",
        "0001111001100001110000----------",
    ]
    fp_vec_div = ["0-101110010-----001111----------", "0-1011100-1-----111111----------"]

    fp_vec_sqrt = ["0-10111011111001111110----------", "0-1011101-100001111110----------"]

    sve_idiv = [
        "00000100--010100000-------------",  # sve sdiv_z_p_zz_
        "00000100--010110000-------------",  # sve sdivr_z_p_zz_
        "00000100--010101000-------------",  # sve udiv_z_p_zz_
        "00000100--010111000-------------",
    ]  # sve udivr_z_p_zz_

    sve_fdiv = [
        "01100101--001101100-------------",  # sve fdiv_z_p_zz_
        "01100101--001100100-------------",
    ]  # sve fdivr_z_p_zz_

    sve_sqrt = ["01100101--001101101-------------"]  # sve fsqrt_z_p_z_

    # not subject to early-out computation:
    #   '01100101--001111001100----------'  # sve frsqrte_z_z_
    #   '01100101--0-----000111----------'  # sve frsqrts_z_zz_
    #   '01000100--000001101-------------'  # sve ursqrte_z_p_z_

    def __init__(self):
        self.is_int_div = self.build_pattern(DivideSources.int_div)

        self.has_Rn_Rm = self.build_pattern(DivideSources.fp_scalar_div + DivideSources.fp_vec_div)

        self.has_Rn = self.build_pattern(DivideSources.fp_scalar_sqrt + DivideSources.fp_vec_sqrt)

        self.is_sve_idiv = self.build_pattern(DivideSources.sve_idiv)
        self.is_sve_fdiv = self.build_pattern(DivideSources.sve_fdiv)
        self.is_sve_sqrt = self.build_pattern(DivideSources.sve_sqrt)

    @staticmethod
    def build_pattern(patterns):
        pat = _make_pattern()
        for p in patterns:
            pat.add_pattern(p)
        return pat

    def sources(self, pattern) -> str:
        """Return an string for an enum that is used archx to figure
        out where the sources are located in the instruction pattern."""
        test_pattern = _make_pattern()
        test_pattern.add_pattern(pattern)

        if self.is_int_div.p_intersects(test_pattern):
            return "DivSource::GPR_IDIV"
        elif self.has_Rn_Rm.p_intersects(test_pattern):
            return "DivSource::ADVSIMD_FDIV"
        elif self.has_Rn.p_intersects(test_pattern):
            return "DivSource::ADVSIMD_SQRT"
        elif self.is_sve_fdiv.p_intersects(test_pattern):
            return "DivSource::SVE_FDIV"
        elif self.is_sve_sqrt.p_intersects(test_pattern):
            return "DivSource::SVE_SQRT"
        elif self.is_sve_idiv.p_intersects(test_pattern):
            return "DivSource::SVE_IDIV"
        return "DivSource::INVALID"


def trans_is_store(trans) -> str:
    return "true" if any([uop.unit == "st" for uop in trans.uops]) else "false"


def trans_is_load(trans) -> str:
    return "true" if any([uop.unit == "ld" for uop in trans.uops]) else "false"


def trans_is_prefetch(trans) -> str:
    return "true" if any([uop.unit in ["ld", "st"] and uop.prefetch == 1 for uop in trans.uops]) else "false"


def trans_is_acquire(trans) -> str:
    return "true" if any([getattr(uop, "acquire", None) == 1 for uop in trans.uops]) else "false"


def trans_is_release(trans) -> str:
    return "true" if any([getattr(uop, "release", None) == 1 for uop in trans.uops]) else "false"


def trans_is_fusion_delay_hint(trans) -> str:
    return "true" if getattr(trans, "fusion_delay_hint", False) == True else "false"


def trans_mem_tag_checked(trans) -> str:
    mem_tag_checked = getattr(trans, "mem_tag_checked", None)
    if mem_tag_checked == "always":
        return "MemoryTaggingCheck::ALWAYS_CHECKED"
    if mem_tag_checked == "n != 31":
        return "MemoryTaggingCheck::CHECK_IF_NOT_SP"
    return "MemoryTaggingCheck::UNKNOWN"


def pmu_prefix(inp) -> tuple[str, ...]:
    """Add a prefix to each string in the input. Takes list of strings or strings"""
    if isinstance(inp, list):
        return tuple([f"archx::PMU::{x}" for x in inp])
    if isinstance(inp, str):
        return tuple([f"archx::PMU::{inp}"])
    assert 0


ASE_VECTOR_PATTERN = re.compile(r"ASE.*SPEC_VECTOR")
ASE_SCALAR_PATTERN = re.compile(r"ASE.*SPEC_SCALAR")


def _pmu_elaborate(bucket: str, class_nm) -> tuple[str, ...]:
    """Return a tuple of pmu event names given the XLAT bucket argument."""
    shortened = re.sub("_VECTOR", "", bucket)
    shortened = re.sub("_SCALAR", "", shortened)

    out = set([shortened])
    int_spec = False
    if ASE_VECTOR_PATTERN.match(bucket):
        out.add("ASE_VECTOR_SPEC")
    elif ASE_SCALAR_PATTERN.match(bucket):
        out.add("ASE_SCALAR_SPEC")

    if shortened.startswith("ASE"):
        out.add("ASE_SPEC")
        if shortened.startswith("ASE_FP"):
            out.add("ASE_FP_SPEC")
        else:
            out.add("ASE_NONFP_SPEC")
        if shortened.startswith("ASE_INT"):
            out.add("ASE_INT_SPEC")
            int_spec = True

    if "A64SCALAR" in shortened:
        int_spec = True
    elif shortened.startswith("SVE_FP"):
        out.add("SVE_FP_SPEC")
    elif shortened.startswith("SVE_INT"):
        out.add("SVE_INT_SPEC")
        int_spec = True
    if int_spec:  # ase_int + sve_int + a64scalar
        out.add("INT_SPEC")
        if "SVE" not in shortened:
            out.add("INT_FIXED_OPS_SPEC")

    if re.search(r"(ASE_FP|SVE_FP|VFP)", shortened):
        out.add("FP_SPEC")
        if "SVE" not in shortened:  # FP_SPEC - SVE_FP_SPEC
            out.add("FP_FIXED_OPS_SPEC")

    if shortened in ["ASE_SVE_FP_MUL_SPEC", "ASE_SVE_FP_FMA_SPEC"]:
        out.add("ASE_SVE_FP_FMA_MUL_SPEC")
    elif shortened in ["ASE_SVE_FP_DOT_SPEC", "ASE_SVE_FP_MMLA_SPEC"]:
        out.add("ASE_SVE_FP_DOT_MMLA_SPEC")
    elif shortened in ["ASE_SVE_INT_MUL_SPEC", "ASE_SVE_INT_DOT_SPEC", "ASE_SVE_INT_MMLA_SPEC"]:
        out.add("ASE_SVE_INT_MUL_MMLA_DOT_SPEC")
    elif shortened in ["LD_SPEC", "ST_SPEC"]:
        out.add("LDST_SPEC")
    # DP_A64SCALAR is a subset of DP.
    if "DP_A64SCALAR_SPEC" in out:
        out.remove("DP_A64SCALAR_SPEC")
        out.add("DP_SPEC")
    return pmu_prefix(list(sorted(out)))


def _process_pmu_bucket(trans) -> tuple[str, ...]:
    pmu_name = trans.pmu_bucket.upper()
    # print(f"XX {trans.__class__}  {pmu_name}", file=sys.stderr)
    cls = trans.__class__
    if pmu_name in ["LOAD", "LOAD_PREFETCH", "LOAD_ACQUIRE"]:
        return _pmu_elaborate("LD_SPEC", cls)
    if pmu_name in ["STORE", "STORE_RELEASE"]:
        return _pmu_elaborate("ST_SPEC", cls)
    if pmu_name.endswith("SPEC_SCALAR") or pmu_name.endswith("SPEC_VECTOR"):
        return _pmu_elaborate(pmu_name, cls)
    if not pmu_name.endswith("_SPEC"):
        pmu_name = f"{pmu_name}_SPEC"
    return _pmu_elaborate(pmu_name, cls)


def _process_pmu_event_list(trans) -> tuple[str, ...]:
    """As of May 2023, we added a list of explcit events to the XLAT
    so we no longer need to do complex creation of PMU event names
    from the pmu_bucket."""
    pmu_names = [x.upper() for x in trans.pmu_events]
    # A64SCALAR_SPEC was accidentally added to RTL and will be removed in future updates
    pmu_names = [x for x in pmu_names if x != "A64SCALAR_SPEC"]
    return pmu_prefix(sorted(pmu_names))


def trans_instr_pmu(trans) -> tuple[str, ...]:
    """Create a tuple of PMU events using pmu.h names from the xlat pmu_bucket."""
    cls = trans.__class__  # for debugging
    lst = tuple()
    if hasattr(trans, "pmu_events"):
        # has priority over older pmu_bucket variable
        lst = _process_pmu_event_list(trans)
        # print(f'pmulst cls {str(cls):20} {lst}', file=sys.stderr)
    elif hasattr(trans, "pmu_bucket"):
        # retaining this for backwards compatability with older xlat if ever needed.
        lst = _process_pmu_bucket(trans)
        # print(f'pmubuckt cls {str(cls):20} {lst}', file=sys.stderr)
    return lst  # may be an empty tuple


def trans_instr_movprfx(trans) -> tuple[bool, bool]:
    """Return a tuple of bools indicating if we can fuse with movprfx predicated or movprfx unpredicated"""
    cls = trans.__class__  # for debugging
    lst = tuple()
    can_fuse_movprfx_predicated = False
    can_fuse_movprfx_unpredicated = False
    if hasattr(trans, "movprfx_fusion"):
        if trans.movprfx_fusion == "unpredicated":
            can_fuse_movprfx_unpredicated = True
        elif trans.movprfx_fusion == "predicated":
            can_fuse_movprfx_predicated = True
            can_fuse_movprfx_unpredicated = True
    return can_fuse_movprfx_predicated, can_fuse_movprfx_unpredicated


def dump(obj):
    for attr in dir(obj):
        if hasattr(obj, attr):
            print("obj.%s = %s" % (attr, getattr(obj, attr)))


def build_inst_info_fusion() -> dict:  # EXTERNAL ENTRY
    """For fused instructions, create a table of data and the bit
    patterns that point to them for dectab.  The table contains groups
    of instruction properties. Returns a dict indexed by tuples of the
    payload data pointing to bit patterns that map to that
    tuple."""
    inst_info_buckets = collections.defaultdict(set)
    for trans, _ in translations:
        if not isinstance(trans, Fusion):
            continue
        pattern = " ".join([t.pattern for t in trans.translations])
        pmu_list = trans_instr_pmu(trans)
        inst_info_buckets[(pmu_list,)].add(pattern)
    return inst_info_buckets


def build_inst_info() -> dict:  # EXTERNAL ENTRY
    """Create a table of data and the instruction bit patterns that
    point to them for dectab.  The table contains groups of
    instruction properties. Returns a dict indexed by tuples of the
    payload data pointing to bit patterns that map to that tuple.
    Does not handle fusion."""

    inst_info_buckets = collections.defaultdict(set)
    branch_detector = BranchDetector()
    barrier_detector = BarrierDetector()
    move_detector = MoveDetector()
    divide_sources = DivideSources()

    def tf2str(x: bool) -> str:
        """Convert python boolean to the C/C++ token for a boolean"""
        if x:
            return "true"
        return "false"

    for trans, _ in translations:
        if isinstance(trans, Fusion):
            continue

        pattern = trans.pattern
        cls = trans.__class__.__name__

        br_type = branch_detector.branch_type(pattern)
        barrier_type = barrier_detector.barrier_type(pattern)
        move_type = move_detector.move_type(pattern)
        is_load = trans_is_load(trans)
        is_store = trans_is_store(trans)
        is_prefetch = trans_is_prefetch(trans)
        is_movprfx_fusible_predicated, is_movprfx_fusible_unpredicated = trans_instr_movprfx(trans)
        is_instr_pmu = trans_instr_pmu(trans)
        is_acquire = trans_is_acquire(trans)
        is_release = trans_is_release(trans)
        div_sources = divide_sources.sources(pattern)
        fusion_delay_hint = trans_is_fusion_delay_hint(trans)
        mem_tag_checked = trans_mem_tag_checked(trans)

        # print(f'{cls:30} {fusion_delay_hint=:5}', file=sys.stderr)
        # if 'true' == fusion_delay_hint:
        #    print(f'{pattern:s}', file=sys.stderr)

        # the tuple is key for table to group patterns that map to that set of values.
        index_tuple = (
            br_type,
            barrier_type,
            is_load,
            is_store,
            is_prefetch,
            is_acquire,
            is_release,
            tf2str(is_movprfx_fusible_predicated),
            tf2str(is_movprfx_fusible_unpredicated),
            move_type,
            is_instr_pmu,
            div_sources,
            fusion_delay_hint,
            mem_tag_checked,
        )
        # print(f'PMUD {cls:20s} {is_instr_pmu}', file=sys.stderr)
        inst_info_buckets[index_tuple].add(pattern)

    return inst_info_buckets


def buckets_to_entries(buckets) -> str:  # EXTERNAL ENTRY
    """Given a list of tuples describing the content of the table,
    create a C++ string to initialize the table."""
    entries = []
    # Add Null Entry
    entries.append("{ /*empty*/ }")

    def format_bucket_part(part):
        if isinstance(part, (list, tuple)):
            # convert tuple to comma-separated list for std::vector initialization in C++
            return "{" + ",".join(part) + "}"
        return part

    for bucket in buckets:
        entries.append("{" + ", ".join(map(format_bucket_part, bucket)) + "}")

    return ",\n    ".join(entries)


def fusion_buckets_to_entries(buckets) -> str:  # EXTERNAL ENTRY
    entries = []
    # Add Null Entry
    entries.append("{ { /*empty*/ } }")

    for bucket in buckets:
        # convert tuple to comma-separated list for std::vector initialization in C++
        vec_init = ",".join(bucket[0])
        entries.append(f"{{  {{ {vec_init} }} }}")
    return ",\n    ".join(entries)


if __name__ == "__main__":
    main()
