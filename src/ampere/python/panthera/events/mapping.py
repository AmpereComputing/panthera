# Copyright (c) 2019-2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import logging
import re
import struct
from abc import ABC, abstractmethod
from types import MethodType
from typing import Callable

import capstone

try:
    import adis

    use_adis = True
except:
    use_adis = False

from panthera.enums import enum_map
from panthera.streams.event_stream import Event

disassembler = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)


def disasm_single_instruction(value):
    try:
        if use_adis:
            return adis.dis(value)
        else:
            instructions = list(disassembler.disasm(struct.pack("<I", value), 0x1000))
            assert len(instructions) == 1
            inst = instructions[0]

            return inst.mnemonic + " " + inst.op_str
    except:
        return "Illegal instruction"


class DynamicAbbreviation(ABC):
    def __init__(self, patterns: list[str], exclude_patterns: list[str] | None):
        self.patterns = [re.compile(pattern) for pattern in patterns]
        self.exclude_patterns = [re.compile(pattern) for pattern in exclude_patterns] if exclude_patterns else []

    def value_with_suffix(self, event: Event, suffix: str):
        for name, value in event.data.items():
            if name.endswith(suffix):
                return value

    def might_generate(self, name: str) -> bool:
        """
        Given the name of an event, determine if this class might be
        able to generate an abbreviation for the given event
        """
        return any(pattern.search(name) for pattern in self.patterns) and not any(
            pattern.search(name) for pattern in self.exclude_patterns
        )

    @abstractmethod
    def generate(self, event: Event) -> str | None:
        """
        Attempt to generate the abbreviation for the supplied event.
        Returns a non-empty string if it supplied the abbreviation and
        None if it did not.
        """


class ValueStringAbbreviation(DynamicAbbreviation):
    def __init__(
        self,
        patterns: list[str],
        value_suffix: str,
        exclude: list[str] | None = None,
        transform: Callable[[str], str] | None = None,
    ):
        self.value_suffix = value_suffix
        self.transform = transform
        super().__init__(patterns, exclude)

    def generate(self, event: Event) -> str | None:
        value = self.value_with_suffix(event, self.value_suffix)
        if value is None:
            return None
        value = str(value)
        if self.transform is not None:
            value = self.transform(value)
        return value


class ValueListAbbreviation(DynamicAbbreviation):
    def __init__(self, patterns: list[str], value_suffix: str, value_list: list[str], exclude: list[str] | None = None):
        self.value_suffix = value_suffix
        self.value_list = value_list
        super().__init__(patterns, exclude)

    def generate(self, event: Event) -> str | None:
        value = self.value_with_suffix(event, self.value_suffix)
        if value is None:
            return None
        try:
            index = int(value)
            return self.value_list[index]
        except IndexError:
            logging.warning(f"Index of {index} for event named {event.name} out-of-bounds for rename")
            return value
        except ValueError:
            return value


class ValueMapAbbreviation(DynamicAbbreviation):
    def __init__(
        self,
        patterns: list[str],
        value_suffix: str,
        value_map: dict[int, str],
        default=None,
        exclude: list[str] | None = None,
        default_on_missing_value_suffix=False,
    ):
        self.value_suffix = value_suffix
        self.value_map = value_map
        self.default = default
        self.default_on_missing_value_suffix = default_on_missing_value_suffix
        super().__init__(patterns, exclude)

    def generate(self, event: Event) -> str | None:
        value = self.value_with_suffix(event, self.value_suffix)
        if value is None:
            return self.default if self.default_on_missing_value_suffix else None
        try:
            value = int(value)
            return self.value_map[value]
        except KeyError:
            if self.default is None:
                logging.warning(f"Value of {value} for event named {event.name} out-of-bounds for value_map")
            return self.default
        except ValueError:
            return value


class EnumAbbreviation(DynamicAbbreviation):
    def __init__(self, patterns: list[str], value_suffix: str, enum_name: str, exclude: list[str] | None = None):
        self.enum_name = enum_name
        self.enum_map_fn = enum_map[enum_name]
        self.value_suffix = value_suffix
        super().__init__(patterns, exclude)

    def generate(self, event: Event) -> str | None:
        value = self.value_with_suffix(event, self.value_suffix)
        if isinstance(value, int):
            return str(self.enum_map_fn(value))
        return value


class CallableAbbreviation(DynamicAbbreviation):
    def __init__(
        self, patterns: list[str], generate: Callable[[object, Event], str | None], exclude: list[str] | None = None
    ):
        self.generate = MethodType(generate, self)
        super().__init__(patterns, exclude)

    def generate(self, event: Event) -> str | None:
        raise Exception("Should be overridden")


alloc_ids = {
    "sys.soc_0.core_0.ixu.alloc_id",
    "sys.soc_0.core_0.lsu.ld_st_fwd_entry",
    "sys.soc_0.core_0.lsu.ld_raw_entry",
    "sys.soc_0.core_0.lsu.sta_raw_entry",
    "sys.soc_0.core_0.lsu.sta_wakeup_ld",
    "sys.soc_0.core_0.lsu.std_wakeup_ld",
}


def access_abbreviations(self, event: Event):
    # Use 'h' for cache hit, 'm' for cache miss, and capitalized for prefetches
    if self.value_with_suffix(event, "prefetch") == 1:
        return "H" if self.value_with_suffix(event, ".hit") == 1 else "M"
    else:
        return "h" if self.value_with_suffix(event, ".hit") == 1 else "m"


def l2c_process_abbreviation(self, event: Event):
    if self.value_with_suffix(event, "bank") is None or self.value_with_suffix(event, "opcode") is None:
        return None
    opcode = self.value_with_suffix(event, "l2_opcode")
    if isinstance(opcode, int):
        return str(enum_map["l2_opcode"](opcode))
    return opcode


def uop_execute_abbreviation(self, event: Event):
    # Use 'b' for correctly predicted branches, 'm' for mispredicts and
    # the latency for all other uops
    mispredict = self.value_with_suffix(event, "branch_mispredict")
    latency = self.value_with_suffix(event, "uop_latency")
    if mispredict is not None:
        return "m" if mispredict else "b"
    else:
        return str(latency)


def iq_write_abbreviation(self, event: Event):
    instruction_id = self.value_with_suffix(event, ".instruction_id")
    if instruction_id and instruction_id <= 0:
        prefix = "WP "  # wrong path
    else:
        prefix = ""
    ibytes = self.value_with_suffix(event, ".inst_bytes")
    if not ibytes:
        ibytes = self.value_with_suffix(event, "instruction")
    if ibytes:
        return prefix + disasm_single_instruction(ibytes)
    else:
        return prefix + "iq"


def idr_align_abbreviation(self, event: Event):
    uop_fusion = self.value_with_suffix(event, "uop_fusion")
    cleaved = self.value_with_suffix(event, "cleaved")
    if uop_fusion and cleaved:
        return "cf"
    elif uop_fusion:
        return "f"
    elif cleaved:
        return "c"
    else:
        return "a"


def uop_dispatch_abbreviation(self, event: Event):
    disasm = self.value_with_suffix(event, ".idr.uopcode_disasm")
    if disasm:
        return disasm.replace("UOP_", "").lower()


def sf_predict_abbreviation(self, event: Event):
    forward = self.value_with_suffix(event, "forward")
    aux_predict = self.value_with_suffix(event, "aux_predict")
    if forward and aux_predict:
        return "wait_aux"
    elif forward and not aux_predict:
        return "wait"
    elif not forward and aux_predict:
        return "skip_aux"
    else:
        return "skip"


def lsu_pipe_occupant_abbreviation(self, event: Event):
    occupant = self.value_with_suffix(event, "pipe_occupant")
    partition = self.value_with_suffix(event, "pipe_partition")
    return f"{occupant}_{partition}"


dynamic_abbreviations = [
    ValueStringAbbreviation([r"idr\.util_"], value_suffix="sch_occupancy"),
    ValueStringAbbreviation([r"l2c\.fb_used_"], value_suffix="occupancy"),
    ValueMapAbbreviation(
        [r"lsu\.ofb.allocate"],
        value_suffix="prefetch",
        value_map={0: "d", 1: "P"},
        default="?",
        default_on_missing_value_suffix=True,
    ),
    ValueStringAbbreviation([r"lsu\.l1bop\.new_best_offset"], value_suffix="offset"),
    ValueStringAbbreviation([r"lsu\.l1bop\.training_rounds"], value_suffix="uint"),
    ValueStringAbbreviation([r"lsu\.l1bop\.filter_table_replacement"], value_suffix="ctr"),
    ValueStringAbbreviation(
        [r"backend_stall$"],
        value_suffix="backend_stall_cause",
        transform=lambda s: re.sub(r"(\w)(_P(\d))?", r"\1\3", s),
    ),
    ValueStringAbbreviation([r"icf\.btb_clear$"], value_suffix="clear_type"),
    ValueStringAbbreviation([r"bpu\.loop_mode$"], value_suffix="loopmode"),
    CallableAbbreviation([r"store_pipe_occupancy$", r"load_pipe_occupancy$"], generate=lsu_pipe_occupant_abbreviation),
    ValueStringAbbreviation([r"lsu\.sf_predictor\.aux$"], value_suffix="activity"),
    ValueStringAbbreviation([r"uop_ld_recycle$"], value_suffix="ld_recycle_reason"),
    ValueStringAbbreviation([r"uop_st_recycle$", r"uop_sta_recycle$"], value_suffix="st_recycle_reason"),
    ValueStringAbbreviation([r"ixu\.ld_status$"], value_suffix="wake_code"),
    ValueStringAbbreviation([r"l2c\.prefetcher\.launch$"], value_suffix="src"),
    ValueMapAbbreviation([r"l2c\.prefetcher\.rrt_update$"], value_suffix="upgraded", value_map={1: "U"}, default="u"),
    ValueMapAbbreviation([r"l2c\.prefetcher\.train$"], value_suffix="train_by_hit", value_map={1: "T"}, default="t"),
    ValueMapAbbreviation([r"home_node\.access$"], value_suffix="slc_status", value_map={0: "m"}, default="h"),
    CallableAbbreviation([r"access$"], generate=access_abbreviations),
    ValueMapAbbreviation([r"pipeline\.request$"], value_suffix="hit", value_map={1: "h"}, default="m"),
    ValueStringAbbreviation(
        [r"pipeline\.sched$", r"l2c\.arbitrate$", r"l2c\.write_schedule$", r"l2c\.read_schedule$"], value_suffix="bank"
    ),
    CallableAbbreviation([r"l2c\.process$"], generate=l2c_process_abbreviation),
    ValueStringAbbreviation(
        [r"uop_issue$", r"uop_data_available$", r"double_pump_"],
        value_suffix="ixu_pipe",
        transform=lambda s: re.sub(r"(\w)(_P(\d))?", r"\1\3", s).replace("D", "ST").replace("C", "LD"),
    ),
    ValueStringAbbreviation([r"\.ixu\.issue$"], value_suffix="latency"),
    CallableAbbreviation([r"uop_execute$"], generate=uop_execute_abbreviation),
    CallableAbbreviation([r"iq_write$", r"icf\.fetch$"], generate=iq_write_abbreviation),
    CallableAbbreviation([r"idr\.align$"], generate=idr_align_abbreviation),
    EnumAbbreviation([r"idr\.align_limited$"], value_suffix="idr_align_reason", enum_name="idr_align_reason"),
    EnumAbbreviation([r"ixu\.early_exit$"], value_suffix="early_exit_reason", enum_name="early_exit_reason"),
    ValueStringAbbreviation([r"uop_decode$"], value_suffix="uopcode_disasm"),
    CallableAbbreviation([r"idr\.uop_dispatch$"], generate=uop_dispatch_abbreviation),
    ValueMapAbbreviation([r"uop_ld_st_prediction$"], value_suffix="ld_st_prediction", value_map={0: "skip", 1: "wait"}),
    ValueMapAbbreviation(
        [r"uop_ld_sfp_oracle_prediction$"], value_suffix="ld_sfp_predict", value_map={0: "skip", 1: "wait"}
    ),
    CallableAbbreviation([r"sf_predictor\.predict$"], generate=sf_predict_abbreviation),
    ValueStringAbbreviation([r"sf_predictor\.aux$"], value_suffix="activity"),
    ValueStringAbbreviation([r"sf_predictor\.update$"], value_suffix="counter"),
    ValueMapAbbreviation([r"gpc\.uop_commit$"], value_suffix="cleaved", value_map={0: "c", 1: "^"}),
    ValueStringAbbreviation([r"gpc\.flush$", r"icf\.btc_clear$"], value_suffix="flush_type"),
    ValueStringAbbreviation([r"br_train$"], value_suffix="update_type"),
    ValueStringAbbreviation([r"br_train$"], value_suffix="br_type"),
    ValueStringAbbreviation(
        [r"mesh_request$", r"mesh_response$", r"recv_req$", r"request_to_request_queue$"], value_suffix="request_type"
    ),
    ValueStringAbbreviation([r"R0_B", r"R1_B"], exclude=[r"_state"], value_suffix="cmd_str"),
    ValueStringAbbreviation([r"R0_B", r"R1_B"], exclude=[r"_state"], value_suffix="cmd_str_ml"),
    ValueStringAbbreviation(
        [r"mc_0_cmds$", r"mc_1_cmds$", r"mc_data$", r"dfi_data$"], exclude=[r"_state"], value_suffix="cmd_str"
    ),
    ValueStringAbbreviation([r"mc_cmd$", r"dfi_cmd$"], value_suffix="bt_state"),
    ValueStringAbbreviation([r"bypass$"], exclude=[r"ixu"], value_suffix="bypass_id"),
    ValueStringAbbreviation([r"\.mode$"], value_suffix="mc_arb_mode"),
    ValueStringAbbreviation([r"chi_mode$"], value_suffix="chi_mode"),
    ValueStringAbbreviation([r"banktracker_same_bank_count$"], value_suffix="same_bank_count"),
    ValueStringAbbreviation([r"request$"], value_suffix="request_str"),
    ValueStringAbbreviation([r"ref_cmds$"], value_suffix="cmd_str"),
    ValueStringAbbreviation([r"ref_cmds$"], value_suffix="cmd_str_ml"),
    ValueStringAbbreviation([r"row_req$"], value_suffix="row"),
    ValueStringAbbreviation([r"R0_B", r"R1_B"], value_suffix="state_str"),
    ValueStringAbbreviation([r"mc_0_cmd_queue_", r"mc_1_cmd_queue_"], value_suffix="cmd_str"),
]

name_to_abbrev = {
    "assert": "!",
    "sys.soc_0.core_0.bpu.faq_write": "w",
    "sys.soc_0.core_0.dmmu.tlb.access": "T",
    "sys.soc_0.core_0.dmmu.tlb.refill": "R",
    "sys.soc_0.core_0.bpu.feq_write": "w",
    "sys.soc_0.core_0.gpc.brain_dead": "!",
    "sys.soc_0.core_0.gpc.commit_bypass_event": "b",
    "sys.soc_0.core_0.gpc.flush": "!",
    "sys.soc_0.core_0.gpc.instruction_checker_error": "!",
    "sys.soc_0.core_0.gpc.uop_commit": "c",
    "sys.soc_0.core_0.gpc.instruction_commit": "C",
    "sys.soc_0.core_0.gpc.rob_dealloc": "z",
    "sys.soc_0.core_0.icf.br_clear": "#",
    "sys.soc_0.core_0.icf.cache.access": "i$",
    "sys.soc_0.core_0.icf.cache.refill": "i$r",
    "sys.soc_0.core_0.icf.feq_read": "r",
    "sys.soc_0.core_0.icf.fetch": "F",
    "sys.soc_0.core_0.icf.iq_write": "iq",
    "sys.soc_0.core_0.icf.itlb.access": "itlb",
    "sys.soc_0.core_0.icf.itlb.refill": "refill",
    "sys.soc_0.core_0.idr.inst_decode_fail": "!",
    "sys.soc_0.core_0.idr.inst_decode": "I",
    "sys.soc_0.core_0.idr.uop_allocate": "A",
    "sys.soc_0.core_0.idr.uop_decode": "d",
    "sys.soc_0.core_0.immu.tlb.access": "T",
    "sys.soc_0.core_0.immu.tlb.refill": "R",
    "sys.soc_0.core_0.ixu.ixu_flush_entry": "x",
    "sys.soc_0.core_0.ixu.sched_dealloc": "o",
    "sys.soc_0.core_0.ixu.could_issue": "v",
    "sys.soc_0.core_0.ixu.uop_dispatch": "g",
    "sys.soc_0.core_0.ixu.uop_issue": "s",
    "sys.soc_0.core_0.ixu.uop_writeback": "w",
    "sys.soc_0.core_0.ixu.uop_cancel": "x",
    "sys.soc_0.core_0.ixu.uop_issue_cancel": "x",
    "sys.soc_0.core_0.ixu.load_cancel": "x",
    "sys.soc_0.core_0.ixu.wb_bypass": "^",
    "sys.soc_0.core_0.l2c.prefetcher.hit": "H",
    "sys.soc_0.core_0.l2c.prefetcher.hit_upgrade": "h",
    "sys.soc_0.core_0.l2c.prefetcher.fill": "P",
    "sys.soc_0.core_0.l2c.prefetcher.fill_upgrade": "p",
    "sys.soc_0.core_0.l2c.prefetcher.train": "T",
    "sys.soc_0.core_0.l2c.prefetcher.rrt_update": "u",
    "sys.soc_0.core_0.l2c.prefetcher.trigger_bop": "0",
    "sys.soc_0.core_0.l2c.prefetcher.trigger_sbop": "1",
    "sys.soc_0.core_0.l2c.prefetcher.trigger_nextline": "2",
    "sys.soc_0.core_0.l2c.prefetcher.trigger_spatial": "3",
    "sys.soc_0.core_0.l2c.prefetcher.drop": ":",
    "sys.soc_0.core_0.l2c.prefetcher.overflow": "+",
    "sys.soc_0.core_0.l2c.prefetcher.reset_maxscore": "x",
    "sys.soc_0.core_0.l2c.prefetcher.reset_maxrounds": "X",
    "sys.soc_0.core_0.l2c.prefetcher.bad_score_shutdown": "D",
    "sys.soc_0.core_0.l2c.mesh_response": "#",
    "sys.soc_0.core_0.l2c.read_pipeline.access": "$",
    "sys.soc_0.core_0.l2c.read_pipeline.merge": "m",
    "sys.soc_0.core_0.l2c.read_pipeline.refill": "f",
    "sys.soc_0.core_0.l2c.read_pipeline.request": "?",
    "sys.soc_0.core_0.l2c.read_pipeline.return": "^",
    "sys.soc_0.core_0.l2c.read_pipeline.sched": "|",
    "sys.soc_0.core_0.l2c.read_pipeline.mesh_request": "r",
    "sys.soc_0.core_0.l2c.write_pipeline.access": "$",
    "sys.soc_0.core_0.l2c.write_pipeline.combine": "j",
    "sys.soc_0.core_0.l2c.write_pipeline.merge": "m",
    "sys.soc_0.core_0.l2c.write_pipeline.refill": "f",
    "sys.soc_0.core_0.l2c.write_pipeline.request": "?",
    "sys.soc_0.core_0.l2c.write_pipeline.return": "^",
    "sys.soc_0.core_0.l2c.write_pipeline.sched": "|",
    "sys.soc_0.core_0.l2c.write_pipeline.mesh_request": "r",
    "sys.soc_0.core_0.l2c.fb_dealloc": "~",
    "sys.soc_0.core_0.lsu.dl1tlb.access": "t",
    "sys.soc_0.core_0.lsu.dl1tlb.refill": "u",
    "sys.soc_0.core_0.lsu.l1d_cache.access": "$",
    "sys.soc_0.core_0.lsu.l1d_cache.refill": "f",
    "sys.soc_0.core_0.lsu.l2_resp_drop": ":",
    "sys.soc_0.core_0.lsu.lob_ofb_update_give": "*",
    "sys.soc_0.core_0.lsu.lob_ofb_update_take": "*",
    "sys.soc_0.core_0.lsu.lob_sleep_on_existing_req": "-",
    "sys.soc_0.core_0.lsu.lob_write": "@",
    "sys.soc_0.core_0.lsu.mmu_resp_drop": ":",
    "sys.soc_0.core_0.lsu.ofb.deallocate": "~",
    "sys.soc_0.core_0.lsu.ofb.full": "&",
    "sys.soc_0.core_0.lsu.ofb.update": "m",
    "sys.soc_0.core_0.lsu.prefetcher.pcp.dropped": ":",
    "sys.soc_0.core_0.lsu.prefetcher.pcp.launched": "Y",
    "sys.soc_0.core_0.lsu.prefetcher.pcp.page_cross": "4",
    "sys.soc_0.core_0.lsu.prefetcher.pcp.stride_mult_inc": "i",
    "sys.soc_0.core_0.lsu.prefetcher.pcp.stride_mult_dec": "d",
    "sys.soc_0.core_0.lsu.prefetcher.pcp.trigger": "P",
    "sys.soc_0.core_0.lsu.pfb_allocate": "[",
    "sys.soc_0.core_0.lsu.pfb_deallocate": "]",
    "sys.soc_0.core_0.lsu.pfb_full": "*",
    "sys.soc_0.core_0.lsu.sob_write": "@",
    "sys.soc_0.core_0.lsu.uop_ld_cancel": "c",
    "sys.soc_0.core_0.lsu.uop_ld_complete": "L",
    "sys.soc_0.core_0.lsu.uop_ld_execute": "l",
    "sys.soc_0.core_0.lsu.uop_ld_recycle": "y",
    "sys.soc_0.core_0.lsu.uop_ld_st_fwd": ">",
    "sys.soc_0.core_0.lsu.uop_sta_complete": "S",
    "sys.soc_0.core_0.lsu.uop_sta_execute": "a",
    "sys.soc_0.core_0.lsu.uop_sta_ld_complete": "L",
    "sys.soc_0.core_0.lsu.uop_sta_ld_execute": "l",
    "sys.soc_0.core_0.lsu.uop_std_execute": "d",
    "sys.soc_0.core_0.lsu.uop_st_retire_complete": "R",
    "sys.soc_0.core_0.lsu.uop_st_retire_schedule": "r",
    "sys.soc_0.core_0.lsu.uop_st_retire_stall": "y",
    "sys.soc_0.core_0.memory.mem_response_full": "&",
    "sys.soc_0.core_0.xu0_alu.uop_execute": "e",
    "sys.soc_0.core_0.xu1_alu.uop_execute": "e",
    "sys.soc_0.core_0.xu4_alu.uop_execute": "e",
    "sys.soc_0.core_0.xu5_alu.uop_execute": "e",
    "sys.soc_0.core_0.sim_end": "=",
}


class Mapper:
    def __init__(self, event_groups=None, hex_args=None, inst_args=None, inline_for_search=True):
        if event_groups:
            options = "|".join([f"({g})" for g in event_groups])
            self.event_groups = re.compile(f"(?P<prefix>{options})\\.(?P<tail>.*)")
        else:
            self.event_groups = None
        self.regex_hexargs = self._fields_to_regex(hex_args)
        self.regex_instargs = self._fields_to_regex(inst_args)
        self.abbreviation_lookups: dict[str, list[DynamicAbbreviation]] = {}
        self.values_no_modify = set()
        self.inline_for_search = inline_for_search

    def process(self, name: str, *, default_group: str | None = None):
        default_group = default_group or "Events"
        if self.event_groups:
            m = re.match(self.event_groups, name)
            if m:
                return (m.group("prefix"), m.group("tail"))
        return default_group, name

    def event_name_to_abbrev(self, event):
        name = event.name
        if name not in self.abbreviation_lookups:
            self.abbreviation_lookups[name] = [
                abbreviation for abbreviation in dynamic_abbreviations if abbreviation.might_generate(name)
            ]

        for abbreviation in self.abbreviation_lookups[name]:
            abbrev = abbreviation.generate(event)
            if abbrev is not None:
                return abbrev

        if name not in name_to_abbrev:
            name_to_abbrev[name] = name.split(".")[-1][0]
        return name_to_abbrev[name]

    def value_with_suffix(self, event, suffix):
        for name, value in event.data.items():
            if name.endswith(suffix):
                return value

    def rename_from_list(self, index, values, default=None):
        if not isinstance(index, int):
            return index

        try:
            return values[index]
        except IndexError:
            logging.warning(f"Value of {index} out-of-bounds for rename")
            return str(index) if default is None else default

    def rename_suffix_from_list(self, event, suffix, values, default=None):
        try:
            index = int(self.value_with_suffix(event, suffix))
            return values[index]
        except IndexError:
            logging.warning(f"Value of {index} for {suffix} out-of-bounds for rename")
            return str(index) if default is None else default
        except ValueError:
            return index

    def modify_args(self, args: dict):
        changes = {}
        searchable = []

        for name, value in args.items():
            if name in self.values_no_modify:
                continue

            used_for_search = False
            if self.inline_for_search:
                used_for_search = True
                if name == "txid" or name.endswith("waking_tx"):
                    searchable.append("txid_%0d." % value)
                elif name == "unum" or name.endswith(".gpc.unum"):
                    searchable.append("unum_%0d." % value)
                elif name == "inum" or name.endswith(".gpc.inum"):
                    searchable.append("inum_%0d." % value)
                elif name in alloc_ids:
                    changes[name] = "allocid_%0d." % value
                elif name.endswith("sf_predictor.index") or name.endswith("sfp_index"):
                    changes[name] = "sfpid_%d" % value
                else:
                    used_for_search = False

            if used_for_search:
                pass
            elif name.endswith(".srcid") or name.endswith(".tgtid"):
                changes[name] = self._translate_node_id(value)
            elif name in enum_map and isinstance(value, int):
                changes[name] = str(enum_map[name](value))
            elif self.regex_instargs.match(name):
                changes[name] = hex(int(value))
                changes["disassembly"] = disasm_single_instruction(value)
            elif self.regex_hexargs.match(name):
                changes[name] = hex(int(value, base=0) if isinstance(value, str) else value)
            else:
                self.values_no_modify.add(name)

        if searchable:
            changes["search_data"] = "".join(searchable)

        args.update(changes)

    def _fields_to_regex(self, fields):
        re_str = "^((" + ")|(".join(fields) + "))$"
        return re.compile(re_str)

    def _translate_node_id(self, id):
        port = id & 0x7
        x = (id >> 3) & 0xF
        y = (id >> 7) & 0xF
        return f"[{x},{y},{port}]"
