# Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Generate uop info."""

import re
import sys
from enum import Flag, auto


class Pipelines(Flag):
    NONE = 0
    A = auto()
    B = auto()
    BS = auto()
    J = auto()
    K = auto()
    S = auto()
    C = auto()
    D = auto()
    E = auto()
    F = auto()
    X = auto()
    Y = auto()
    Z = auto()
    U = auto()
    V = auto()
    W = auto()


uop_info_tab = []


def tf(x: bool) -> str:
    """Convert Python True/False to "true"/"false" for C++."""
    if x:
        return "true"
    return "false"


class UopInfoType(type):
    _unique_uop_info = {}

    def __call__(cls, uop):
        uop_info = super().__call__(uop)
        if uop_info in UopInfoType._unique_uop_info:
            return UopInfoType._unique_uop_info[uop_info]
        else:
            UopInfoType._unique_uop_info[uop_info] = uop_info
            uop_info.index = len(uop_info_tab)
            uop_info_tab.append(uop_info)
            return uop_info


def py_bool_to_c(x: bool) -> str:
    return "true" if x else "false"


_FSU_UNIT_LATENCY_PATTERN = re.compile(r"(?P<unit>[A-Za-z]+)(?P<latency>[\d]+)")
_FSU_UNIT_PATTERN = re.compile(r"(?P<unit>[A-Za-z]+)")


def _parse_producing_uop(uop, producing_uop):
    """Return unit and latency"""
    try:
        m = _FSU_UNIT_LATENCY_PATTERN.match(producing_uop)
    except:
        raise ValueError(f"Bad string for fsu producing uop {uop.name}: [{producing_uop}]")

    # 2022-11-14 The FsuUops.xlsx has invalid latencies in the Producing_Uop column, so we ignore them for now.
    if m and m.group("unit") != "Dv":
        return m.group("unit"), int(m.group("latency"))
    else:
        m = _FSU_UNIT_PATTERN.match(producing_uop)
        if m:
            if hasattr(uop, "Proposed_latency"):
                return m.group("unit"), int(uop.Proposed_latency)
            else:
                latency = getattr(uop, "latency", getattr(uop, "lat", 1))
                return m.group("unit"), int(latency)
            # raise ValueError(f"No proposed latency for: {uop.name} unit: {producing_uop}")
    raise ValueError(f"Could not parse producing uop: {producing_uop} for {uop.name}")


class UopInfo(metaclass=UopInfoType):
    UNIT_MAP = {
        "iu": "Integer",
        "ixu": "Integer",
        "bu": "Branch",
        "fsu": "FpSIMD",
        "ld": "Load",
        "st": "Store",
        "nop": "None",
        "idr": "IDR",
    }

    MERGE_SOURCE_MAP = {"N": 0, "S1": 1, "S2": 2, "S3": 3}

    # Mapping of FSU unit names to bypass groups
    #   * Hps and Hp are actually the same
    #   * Mas and Ma are actually the same
    #   * Three of the SIMD integer units (Au, Su, Xu) can all bypass to each other without penalty,
    #     so group them all together into SI. Mu is similar but has a unique late-accumulate bypass so
    #     it has to stay separate.
    FSU_UNIT_MAP = {
        "Au": "SI",
        "Bu": "BU",
        "By": "BY",
        "Cu": "CU",
        "Dv": "DV",
        "Hp": "HP",
        "Hps": "HP",
        "Ma": "MA",
        "Mas": "MA",
        "Mu": "MU",
        "Su": "SI",
        "Xu": "SI",
        "Si": "SI",
    }

    FSU_SRCTYPE_MAP = {
        None: "NONE",
        "~": "NONE",
        "": "NONE",
        "n": "NORMAL",
        "n.Tdst": "NORMAL",
        "n.Tsrc": "NORMAL",
        "x": "NO_LOCAL_BYP",
        "x.Tsrc": "NO_LOCAL_BYP",
        "l": "LATE",
        "l.Tdst": "LATE",
        "l.Tsrc": "LATE",
    }

    # Dictionary that represents the legacy unit to pipe map before switching to furiosa uops
    UNIT_PIPE_MAP = {
        "Integer": "AB",
        "IDR": "AB",
        "None": "A",
        "Branch": "A",
        "Load": "C",
        "Store": "D",
        "FpSIMD": "xy",
    }

    def __init__(self, uop):
        self.index = None  # set by the metaclass
        self.unit = self.UNIT_MAP[uop.unit]
        self.fsu_unit = self.FSU_UNIT_MAP[uop.fsu_unit] if hasattr(uop, "fsu_unit") else "NONE"
        self.fsu_crypto_unit = self.FSU_UNIT_MAP[uop.crypto_move] if getattr(uop, "crypto_move", None) else "NONE"

        # identify compare and swap instructions and atomic ops  with GPR dests,
        self.store_with_dest = False
        if self.unit == "Store" and uop.dest_type != "none":
            self.store_with_dest = True

        # producing_uop is new for Rogue. it is used when operations
        # start on one unit (fsu_unit, above) and complete/write-back
        # from a different unit.
        self.producing_uop = "INVALID"  # Si3, Hp3, etc.
        self.fsu_producer_unit = self.fsu_unit  # Prior to trinity, yaml did not have Producing_Uop
        self.fsu_producer_unit_suffix = 0
        self.fsu_producer_latency = 0

        self.dest_no_unr_byp = getattr(uop, "DestInhUnrndByp", None) in {"Y", "y"}

        if hasattr(uop, "Producing_Uop") and uop.Producing_Uop:
            self.producing_uop = uop.Producing_Uop
            producer_unit, self.fsu_producer_latency = _parse_producing_uop(uop, self.producing_uop)

            self.fsu_producer_unit = self.FSU_UNIT_MAP[producer_unit]
            self.fsu_producer_unit_suffix = self.fsu_producer_latency

            # fix the misnamed Ma5 latency (should be "Ma4")
            # and fix the Dv{20,29} latencies for g{s,u}div; they erronously include i2f & f2i uop latencies.
            if self.producing_uop == "Ma5":
                self.fsu_producer_latency = 4
            elif re.match(r"g[su]div", uop.name):
                if self.producing_uop == "Dv29":
                    self.fsu_producer_latency = 64 // 4 + 3
                elif self.producing_uop == "Dv20":
                    self.fsu_producer_latency = 32 // 4 + 3

            # separate the BF16 DOT and MMLA instructions
            if self.producing_uop == "Bu":  # avoids renaming things on Connor onwards (Connor uses Bu2/Bu3)
                if self.fsu_producer_unit == "BU":  # This is trinity specific
                    if "dot" in uop.name:
                        # self.fsu_producer_unit = 'BU_DOT'
                        self.fsu_producer_latency = 2
                    elif "mmla" in uop.name:
                        # self.fsu_producer_unit = 'BU_MMLA'
                        self.fsu_producer_latency = 3
                    else:
                        raise Exception(f"Unexpected Bu unit uop {uop.name}")

        # yaml src{1,2,3}t get mapped to panthera's uop src{0,1,2}
        # operands. For FSU governing predicates come from pred
        # operands and are placed in panthera's src3.  fsu_src_types
        # does not cover the governing predicates that get placed into
        # panthera's src3 operand. For IXU and LSU, governing
        # predicates are placed in src3.
        try:
            fsrc_types = [self.FSU_SRCTYPE_MAP[getattr(uop, src, None)] for src in ["src1t", "src2t", "src3t"]]
        except:
            msg = []
            for src in ["src1t", "src2t", "src3t"]:  # debugging
                a = getattr(uop, src, None)
                if a in self.FSU_SRCTYPE_MAP:
                    b = self.FSU_SRCTYPE_MAP[a]
                    msg.append(f"found {a} in FSU_SRC_TYPE_MAP: {b}")
                else:
                    msg.append(f"did not find [{a}] in FSU_SRC_TYPE_MAP")
            raise ValueError("\n".join(msg))

        self.is_predicated: bool = "Y" in getattr(uop, "predicated", "")
        if self.is_predicated:
            ptype = "NORMAL"
        else:
            ptype = "NONE"
        self.fsu_src_types = tuple(fsrc_types + [ptype])

        if self.fsu_producer_latency:
            self.latency = self.fsu_producer_latency
        else:
            self.latency = self._get_latency(uop)
        if self.fsu_producer_unit_suffix == 0:
            self.fsu_producer_unit_suffix = self.latency
        self.allowed_pipes = self._get_allowed_pipes(uop)
        self.early_exit_latency = self.latency
        self.uop_op_count = self._get_uop_op_count(uop)
        self.pair = getattr(uop, "pair", 0)
        self.atomic = getattr(uop, "atomic", 0)
        self.exclusive = getattr(uop, "exclusive", 0)
        self.acquire = getattr(uop, "acquire", 0)
        self.release = getattr(uop, "release", 0)
        self.nontemporal = getattr(uop, "non_temporal", 0)
        self.offset_lsl = getattr(uop, "offset_lsl", 0)
        self.is_pac_auth = getattr(uop, "is_pac_auth", 0)
        self.is_fsu_uvw = (
            hasattr(uop, "fsu_unit")
            and (uop.fsu_unit == "By")
            and ("Z" not in getattr(uop, "pipe", 0))
            and ("X" in getattr(uop, "pipe", 0))
            and ("Y" in getattr(uop, "pipe", 0))
        )
        self.is_fsu_v = (
            self.is_fsu_uvw and (getattr(uop, "src2_type", 0) == "none") and (getattr(uop, "src3_type", 0) == "none")
        )

        self.prefetch = "NONE"
        self.prefetch_level = getattr(uop, "prefetch_level", "L1").upper()
        if getattr(uop, "prefetch", 0):
            if getattr(uop, "prefetch_state", "") == "exclusive" or uop.name.startswith("prfm_st"):
                self.prefetch = "STORE"
            else:
                self.prefetch = "LOAD"

        self.mem_datasize = self._get_mem_datasize(uop, self.pair)
        self.store_type = self._get_store_type(uop)
        self.barrier_type = self._get_barrier_type(uop)
        self.drain_type = self._get_drain_type(uop)
        self.resource_type, self.resource_rate, self.resource_count = self._get_resource_info(uop)
        self.early_exit_resource_rate = self.resource_rate
        self.vector_element_count, self.vector_element_size = self._get_element_info(uop)
        self.is_i2f = self.unit != "Load" and self._has_vec_dest(uop) and self._has_int_src(uop)
        self.is_f2i = self._has_int_dest(uop) and self._has_vec_src(uop)

        just_x_pipe = ["X"]
        pipe = getattr(uop, "pipe", ["UNKN"])
        if uop.name.startswith("g") and pipe == just_x_pipe:
            self.is_fpu_gpr_x_to_z = True
        else:
            self.is_fpu_gpr_x_to_z = False
        # print(f'uop: {uop.name}  pipe: {pipe} gpr_x2z: {self.is_fpu_gpr_x_to_z}', file=sys.stderr)

        if uop.name.startswith("stdv") and pipe == just_x_pipe:
            self.is_stdv_x_to_z = True
        else:
            self.is_stdv_x_to_z = getattr(uop, "stdv_x_to_z", False)

        self.is_mfspr = "mfspr" in uop.name
        self.is_mtspr = "mtspr" in uop.name
        self.is_fdiv = "fdiv" in uop.name

        if (
            "udiv" in uop.name  # legacy and sve
            or "sdiv" in uop.name  # legacy and sve
            or "fidiv" in uop.name
        ):  # AdvSIMD
            self.is_idiv = True
        else:
            self.is_idiv = False

        self.is_fsqrt = "fsqrt" in uop.name
        # predicated field, valid values: Y1,Y2,Y3,Y, N. Convert Y* to True
        self.is_predicated: bool = "Y" in getattr(uop, "predicated", "")

        self.is_predicated_sampled = True if getattr(uop, "predicated_sampled", "N") == "Y" else False

        sparse = getattr(uop, "sparse", 0)
        gather = getattr(uop, "gather", 0)
        scatter = getattr(uop, "scatter", 0)
        self.is_gather2 = False
        self.is_gather4 = False
        self.is_scatter2 = False
        self.is_scatter4 = False
        if gather:
            self.is_gather2 = sparse == 2
            self.is_gather4 = sparse == 4
        if scatter:
            self.is_scatter2 = sparse == 2
            self.is_scatter4 = sparse == 4

        self.is_double_pump = True if getattr(uop, "dbl_pump", "N") == "Y" else False
        self.nep_dest_as_src = True if getattr(uop, "AFP_NEP", "N") == "YD" else False

        self.multiple_memop = "MultipleMemopType::NONE"
        nrefs = getattr(uop, "nrefs", 0)
        nref = getattr(uop, "nref", 0)
        if nrefs > 1:
            if getattr(uop, "gather", 0) == 0 and getattr(uop, "scatter", 0) == 0:
                store_type = getattr(uop, "store_type", "NONE")
                if store_type == "STA":  # ignore STD uops
                    self.multiple_memop = f"MultipleMemopType::ST{nrefs}"
                elif store_type == "NONE":
                    self.multiple_memop = f"MultipleMemopType::LD{nrefs}"

        self.sequence_number = 0
        if nref > 0:
            self.sequence_number = nref - 1  # 0...N-1

        # which source gets replaced or dropped when doing SVE movprfx fusion
        self.sve_merge_source = self.MERGE_SOURCE_MAP[getattr(uop, "merge_src", "N")]

    def __format__(self, format_spec):
        def as_cpp_bool(value):
            return "true" if value else "false"

        dest_no_unr_byp = as_cpp_bool(self.dest_no_unr_byp)
        is_i2f = as_cpp_bool(self.is_i2f)
        is_f2i = as_cpp_bool(self.is_f2i)
        is_mfspr = as_cpp_bool(self.is_mfspr)
        is_mtspr = as_cpp_bool(self.is_mtspr)
        is_fdiv = as_cpp_bool(self.is_fdiv)
        is_fsqrt = as_cpp_bool(self.is_fsqrt)
        is_idiv = as_cpp_bool(self.is_idiv)
        is_nontemporal = as_cpp_bool(self.nontemporal)
        is_pac_auth = as_cpp_bool(self.is_pac_auth)
        is_fsu_uvw = as_cpp_bool(self.is_fsu_uvw)
        is_fsu_v = as_cpp_bool(self.is_fsu_v)

        assert self.latency is not None
        assert self.early_exit_resource_rate is not None
        # layout must match src/ampere/archx/uop_info.h struct UopInfo
        return (
            f"/* {self.index} */ {{ Uopunit::{self.unit}, "
            f"FsuUnit::{self.fsu_unit}, "  # fsu_unit consuming
            f"FsuUnit::{self.fsu_producer_unit}, "  # fsu_unit_producing
            f"{self.fsu_producer_unit_suffix}, "  # fsu_unit_producing_suffix
            f"FsuUnit::{self.fsu_crypto_unit}, "  # fsu_crypto_unit fsu unit when in crypto mode
            # fill in 4 element array
            f"{{ FsuSrcType::{self.fsu_src_types[0]}, "
            f"FsuSrcType::{self.fsu_src_types[1]}, "
            f"FsuSrcType::{self.fsu_src_types[2]}, "
            f"FsuSrcType::{self.fsu_src_types[3]} }}, "
            f"{tf(self.store_with_dest)}, "
            f"{dest_no_unr_byp}, /*pipes*/{self.allowed_pipes.value}, "
            f" /*latency*/ {self.latency}, {self.early_exit_latency}, /*memsz*/ {self.mem_datasize}, {self.store_type}, "
            f"{self.barrier_type}, {self.drain_type}, SWPrefetchType::{self.prefetch}, SWPrefetchLevel::{self.prefetch_level}, "
            f"{is_nontemporal}, {self.offset_lsl}, {self.resource_type}, "
            f"{self.resource_rate}, {self.early_exit_resource_rate}, {self.resource_count}, "
            f"{self.vector_element_count}, {self.vector_element_size}, "
            f"{self.uop_op_count}, "
            f"{self.pair}, {self.atomic}, {self.exclusive}, {self.acquire}, {self.release}, "
            f"/*i2f*/{is_i2f}, /*f2i*/{is_f2i}, {is_mfspr}, {is_mtspr}, /*fdiv*/{is_fdiv}, /*sqrt*/{is_fsqrt}, "
            f"/*idiv*/{is_idiv}, "
            f"/*is_pac_auth*/{is_pac_auth}, "
            f"/*is_fsu_uvw*/{is_fsu_uvw}, "
            f"/*is_fsu_v*/{is_fsu_v}, "
            f"/*predicated*/ {py_bool_to_c(self.is_predicated)}, "
            f"/*predicated_sampled*/ {py_bool_to_c(self.is_predicated_sampled)}, "
            f"/*fpu_gpr_x_to_z*/ {py_bool_to_c(self.is_fpu_gpr_x_to_z)}, "
            f"/*stdv_x_to_z*/ {py_bool_to_c(self.is_stdv_x_to_z)}, "
            f"/*gather2*/ {py_bool_to_c(self.is_gather2)}, "
            f"/*gather4*/ {py_bool_to_c(self.is_gather4)}, "
            f"/*scatter2*/ {py_bool_to_c(self.is_scatter2)}, "
            f"/*scatter4*/ {py_bool_to_c(self.is_scatter4)}, "
            f"/*double_pump*/ {py_bool_to_c(self.is_double_pump)}, "
            f"/*nep_dest_as_src*/ {py_bool_to_c(self.nep_dest_as_src)}, "
            f"/*multiple_memop*/ {self.multiple_memop}, "
            f"/*sequence_number*/ {self.sequence_number}, "
            f"/*sve_merge_source*/ {self.sve_merge_source}, "
            f"}}"
        )

    def __hash__(self):
        return hash(tuple([self.__dict__[k] for k in sorted(self.__dict__.keys()) if k != "index"]))

    def __eq__(self, other):
        a = [(k, self.__dict__[k]) for k in sorted(self.__dict__.keys()) if k != "index"]
        b = [(k, other.__dict__[k]) for k in sorted(other.__dict__.keys()) if k != "index"]
        return a == b

    @staticmethod
    def _get_latency(uop):
        latency = getattr(uop, "latency", getattr(uop, "lat", 1))
        assert latency > 0
        return latency

    @staticmethod
    def _get_mem_elem_size(uop):  # not used yet
        if hasattr(uop, "mem_elem_size"):
            return uop.mem_elem_size
        return 0

    @staticmethod
    def _get_mem_datasize(uop, is_pair):
        """Total span of memory reference."""
        if hasattr(uop, "mem_datasize"):
            mem_datasize = {"z": 64, "b": 1, "h": 2, "w": 4, "d": 8, "q": 16}[uop.mem_datasize]
        elif hasattr(uop, "mem_width"):
            mem_datasize = uop.mem_width
        else:
            mem_datasize = 0

        if is_pair:
            mem_datasize *= 2

        return mem_datasize

    @staticmethod
    def _has_vec_dest(uop):
        return getattr(uop, "dest_type", 0) == "vec" or getattr(uop, "dest2_type", 0) == "vec"

    @staticmethod
    def _has_vec_src(uop):
        return (
            getattr(uop, "src1_type", 0) == "vec"
            or getattr(uop, "src2_type", 0) == "vec"
            or getattr(uop, "src3_type", 0) == "vec"
        )

    @staticmethod
    def _has_int_dest(uop):
        return getattr(uop, "dest_type", 0) == "gpr" or getattr(uop, "dest2_type", 0) == "gpr"

    @staticmethod
    def _has_int_src(uop):
        return (
            getattr(uop, "src1_type", 0) == "gpr"
            or getattr(uop, "src2_type", 0) == "gpr"
            or getattr(uop, "src3_type", 0) == "gpr"
        )

    def _get_allowed_pipes(self, uop):
        allowed_pipes = Pipelines.NONE
        if hasattr(uop, "pipe"):
            for p in uop.pipe:
                allowed_pipes |= Pipelines[p.upper()]
        else:
            # In practice, this should only be used for IDR-executed uops and some unit tests
            for p in UopInfo.UNIT_PIPE_MAP[self.unit]:
                allowed_pipes |= Pipelines[p.upper()]
        return allowed_pipes

    def _get_store_type(self, uop):
        if self.unit == "FpSIMD" and "std" in uop.name:
            store_type = "StoreType::" + "STDVHI" if "stdvhi" in uop.name else "StoreType::STDV"
        else:
            store_type = "StoreType::" + getattr(uop, "store_type", "SUS" if self.unit == "Store" else "NONE")
        return store_type

    @staticmethod
    def _get_barrier_type(uop):
        if "dmb_all" in uop.name:
            barrier_type = "BarrierType::DMB_ALL"
        elif "dmb_st" in uop.name:
            barrier_type = "BarrierType::DMB_ST"
        elif "dsb" in uop.name:
            barrier_type = "BarrierType::DSB"
        else:
            barrier_type = "BarrierType::NONE"
        return barrier_type

    @staticmethod
    def _get_drain_type(uop):
        if "eret" in uop.name or "wfe" in uop.name or "wfi" in uop.name:
            return "DrainType::POST_STALL"
        elif "dsb" in uop.name:
            return "DrainType::POST_DRAIN"
        else:
            return "DrainType::NONE"

    @staticmethod
    def _get_resource_info(uop):
        """Resource rate is given as a delta from the overall latency. For example 64-bit integer divide latency
        is 35 cycles and the divider resource rate is 1, so we can start a new divide every 35+1=36 cycles.
        To support using both halves of the vector FP divider independently for scalar divides, we also have a
        resource_count. For FP divider uops, scalar and short-vector uops use 1 resource, and long vectors use 2.
        """
        resource_type = "ResourceType::NONE"
        resource_rate = 0
        resource_count = 0
        if hasattr(uop, "exe_unit"):
            if uop.exe_unit == "div":
                resource_type = "ResourceType::INT_DIVIDER"
                resource_rate = 1
                resource_count = 1
        elif hasattr(uop, "fsu_unit"):
            if uop.fsu_unit == "Dv":
                resource_type = "ResourceType::FP_DIVIDER"
                resource_rate = 0
                if uop.name.endswith("_lv") or uop.name.endswith("_pv"):
                    resource_count = 2
                else:
                    resource_count = 1
        return resource_type, resource_rate, resource_count

    XLATE_MEMSZ = {"b": 1, "h": 2, "w": 4, "s": 4, "d": 8, "q": 16}

    def _get_element_info(self, uop):
        element_count = 0
        element_size = 0
        vector_len_bits = 128
        if hasattr(uop, "vector_elem_size") and uop.vector_elem_size != "none":
            # sve lsu uops have vector_elem_size
            element_size = UopInfo.XLATE_MEMSZ[uop.vector_elem_size] * 8
            element_count = vector_len_bits // element_size
        elif self.unit == "FpSIMD":
            if uop.Tdst == "B":
                element_size = 8
            elif uop.Tdst == "H":
                element_size = 16
            elif uop.Tdst == "S":
                element_size = 32
            elif uop.Tdst == "D":
                element_size = 64

            elif "_d_" in uop.name:
                element_size = 64
            elif "_s_" in uop.name:
                element_size = 32
            elif "_h_" in uop.name:
                element_size = 16
            elif "_b_" in uop.name:
                element_size = 8

            if "_nv" in uop.name:
                element_count = 1
            elif "_sv" in uop.name:
                element_count = 64 // element_size
            elif "_lv" in uop.name:
                element_count = 128 // element_size
            elif "_pv" in uop.name or "_uv" in uop.name:
                # sve fsu uops do not have the vector_elem_size set.
                element_count = vector_len_bits // element_size
        return element_count, element_size

    @staticmethod
    def _get_uop_op_count(uop):
        if hasattr(uop, "uop_op_count"):
            return uop.uop_op_count
        return 0
