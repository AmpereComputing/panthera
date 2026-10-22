# Copyright (c) 2019-2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import re

from catscan.events.mapping import CallableAbbreviation, ValueStringAbbreviation
from perf_streams.event_stream import Event


def setup_disassembler(self, mapper):
    self.disassembler = mapper.disasm_single_instruction


def access_abbreviations(self, event: Event):
    # Use 'h' for cache hit, 'm' for cache miss, and capitalized for prefetches
    if self.value_with_suffix(event, "prefetch") == 1:
        return "H" if self.value_with_suffix(event, ".hit") == 1 else "M"
    else:
        return "h" if self.value_with_suffix(event, ".hit") == 1 else "m"


def l2c_process_abbreviation(self, event: Event):
    if self.value_with_suffix(event, "bank") is None or self.value_with_suffix(event, "opcode") is None:
        return None
    return self.value_with_suffix(event, "l2_opcode")


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
        return prefix + self.disassembler(ibytes)
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


add_abbreviation(
    ValueStringAbbreviation(
        [r"backend_stall$"],
        value_suffix="backend_stall_cause",
        transform=lambda s: re.sub(r"(\w)(_P(\d))?", r"\1\3", s),
    )
)
add_abbreviation(
    CallableAbbreviation([r"store_pipe_occupancy$", r"load_pipe_occupancy$"], generate=lsu_pipe_occupant_abbreviation)
)
add_abbreviation(CallableAbbreviation([r"access$"], generate=access_abbreviations))
add_abbreviation(CallableAbbreviation([r"l2c\.process$"], generate=l2c_process_abbreviation))
add_abbreviation(
    ValueStringAbbreviation(
        [r"uop_issue$", r"uop_data_available$", r"double_pump_"],
        value_suffix="ixu_pipe",
        transform=lambda s: re.sub(r"(\w)(_P(\d))?", r"\1\3", s).replace("D", "ST").replace("C", "LD"),
    )
)
add_abbreviation(CallableAbbreviation([r"uop_execute$"], generate=uop_execute_abbreviation))
add_abbreviation(
    CallableAbbreviation([r"iq_write$", r"icf\.fetch$"], generate=iq_write_abbreviation, setup=setup_disassembler)
)
add_abbreviation(CallableAbbreviation([r"idr\.align$"], generate=idr_align_abbreviation))
add_abbreviation(CallableAbbreviation([r"idr\.uop_dispatch$"], generate=uop_dispatch_abbreviation))
add_abbreviation(CallableAbbreviation([r"sf_predictor\.predict$"], generate=sf_predict_abbreviation))
