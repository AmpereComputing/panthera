# Copyright (c) 2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from ampere.xlat.json_encoding import JsonEncoding


def value(bits):
    return {"_type": "Values.Value", "meaning": None, "value": f"'{bits}'"}


def field(name, start, width, bits, mask):
    return {
        "_type": "Instruction.Encodeset.Field",
        "name": name,
        "range": {"_type": "Range", "start": start, "width": width},
        "value": value(bits),
        "should_be_mask": value(mask),
    }


def test_json_encoding_uses_mask_bits_constraints_and_features():
    instruction = {
        "_type": "Instruction.Instruction",
        "name": "TEST",
        "assembly": {
            "_type": "Instruction.Assembly",
            "symbols": [
                {"_type": "Instruction.Symbols.Literal", "value": "TEST"},
            ],
        },
        "encoding": {
            "_type": "Instruction.Encodeset.Encodeset",
            "width": 32,
            "values": [
                field("op", 28, 4, "1010", "0000"),
                field("reserved", 24, 4, "0011", "1111"),
                field("imm", 0, 24, "x" * 24, "1" * 24),
            ],
        },
        "condition": {
            "_type": "AST.BinaryOp",
            "left": {
                "_type": "AST.BinaryOp",
                "left": {"_type": "AST.Identifier", "value": "imm"},
                "op": "!=",
                "right": value("0" * 24),
            },
            "op": "&&",
            "right": {
                "_type": "AST.Function",
                "name": "IsFeatureImplemented",
                "arguments": [{"_type": "AST.Identifier", "value": "FEAT_TEST"}],
                "parameters": [],
            },
        },
    }

    encoding = JsonEncoding.from_instruction(instruction)

    assert encoding.mnemonic == "TEST"
    assert encoding.features == ("FEAT_TEST",)
    assert encoding.feature == "FEAT_TEST"
    assert encoding.parts[0].bits == "1010"
    assert encoding.parts[1].bits == "----"
    assert encoding.parts[2].pattern == "(------------------------ ! 000000000000000000000000)"
    assert "def decode(self, f):" in encoding.render()
