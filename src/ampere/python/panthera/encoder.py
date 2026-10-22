# Copyright (c) 2020 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os

from ampere.xlat.fields import NeedDefiniteValue
from ampere.xlat.translation import Fusion, translations

PROJECT_PATH = os.path.join(os.path.dirname(__file__), "..", "..", "..", "..")


class CreateEncoderClass:
    HFILE_TEMPLATE = """//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#ifndef AMPERE_ARCHX_{ns_upper}_ENCODE_H
#define AMPERE_ARCHX_{ns_upper}_ENCODE_H

#include <cstdint>
#include <map>
#include <string>

extern uint32_t {ns}_encode(const std::string& op, const std::map<std::string, uint32_t>& fields);

#endif // AMPERE_ARCHX_{ns_upper}_ENCODE_H
"""

    CCFILE_TEMPLATE = """//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "{ns}_encode.h"

{encode_func_table}

using encode_func = uint32_t (*)(const std::map<std::string, uint32_t>&);

static std::map<std::string, encode_func> encode_index = {{
    {encode_index}
}};

uint32_t {ns}_encode(const std::string& op, const std::map<std::string, uint32_t>& fields)
{{
    auto func = encode_index.at(op);
    return (*func)(fields);
}}

"""

    def __init__(self, ns_name):
        super().__init__()
        self.ns_name = ns_name
        self.insn_index = 0
        self.encode_index = []
        self.encode_func_table = []
        self.instructions = set()

    def generate_outputs(self):
        with open(f"{self.ns_name}_encode.h", "w") as hfile_out:
            hfile_out.write(CreateEncoderClass.HFILE_TEMPLATE.format(ns=self.ns_name, ns_upper=self.ns_name.upper()))

        with open(f"{self.ns_name}_encode.cc", "w") as ccfile_out:
            ccfile_out.write(
                CreateEncoderClass.CCFILE_TEMPLATE.format(
                    ns=self.ns_name,
                    encode_index=",\n    ".join(self.encode_index),
                    encode_func_table="\n".join(self.encode_func_table),
                )
            )

    def handle_instruction(self, insn):
        insn_name = insn.__class__.__name__

        if insn_name in self.instructions:
            return

        def combine(b1, b2):
            return "-" if b1 != b2 else b1

        def dontcare_or(p1: str, p2: str) -> str:
            return "".join([combine(b1, b2) for b1, b2 in zip(p1, p2)])

        generic_pattern = insn.__class__.patterns[0]
        for pattern in insn.__class__.patterns[1:]:
            generic_pattern = dontcare_or(generic_pattern, pattern)

        variable_fields = []
        for field in insn.fields.__dict__.values():
            sub_pattern = generic_pattern[31 - field.hi : 31 - field.lo + 1]
            if "-" in sub_pattern:
                variable_fields.append(field)

        self.instructions.add(insn_name)
        self.encode_index.append(f'{{"{insn_name}", encode_{self.insn_index}}}')

        base_opcode = int(generic_pattern.replace("-", "0"), 2)
        fields = []

        for field in variable_fields:
            fields.append(f'opcode |= fields.at("{field.name}") << {field.lo};')

        field_assignments = "\n    ".join(fields)

        relative_source_file = os.path.relpath(insn.filename, start=PROJECT_PATH)
        self.encode_func_table.append(
            """
// {source_file}:{line}
static uint32_t encode_{index}(const std::map<std::string, uint32_t>& fields [[maybe_unused]])
{{
    uint32_t opcode = 0x{base_opcode:08x};
    {field_assignments}
    return opcode;
}}
""".format(
                index=self.insn_index,
                base_opcode=base_opcode,
                field_assignments=field_assignments,
                source_file=relative_source_file,
                line=insn.lineno,
            )
        )

        self.insn_index += 1


def create_encoder(create_encoder_class):
    for trans, _ in translations:
        if not isinstance(trans, Fusion):
            create_encoder_class.handle_instruction(trans)
