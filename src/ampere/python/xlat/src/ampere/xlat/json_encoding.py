# Copyright (c) 2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Read the machine-readable Arm instruction JSON used by ``json2xlat``.

This module exposes the information needed to construct an XLAT translation:
its encoding, named fields, feature requirements, and assembly mnemonic.
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, Iterable, List, Mapping, Optional, Sequence, Tuple


class JsonEncodingError(ValueError):
    """The JSON cannot be represented by the XLAT encoding language."""


JsonObject = Mapping[str, Any]


@dataclass(frozen=True)
class EncodingPart:
    """One contiguous range in a 32-bit instruction encoding."""

    hi: int
    width: int
    bits: str
    field_name: str
    excluded: Optional[str] = None

    @property
    def lo(self) -> int:
        return self.hi - self.width + 1

    @property
    def pattern(self) -> str:
        if self.excluded is None:
            return self.bits
        return f"({self.bits} ! {self.excluded})"


@dataclass(frozen=True)
class JsonEncoding:
    """JSON-derived metadata for one concrete AArch64 instruction encoding."""

    name: str
    mnemonic: str
    parts: Tuple[EncodingPart, ...]
    features: Tuple[str, ...]
    feature_expression: Optional[str]

    @classmethod
    def from_instruction(cls, instruction: JsonObject) -> "JsonEncoding":
        try:
            name = instruction["name"]
            encoding = instruction["encoding"]
            values = encoding["values"]
        except KeyError as error:
            raise JsonEncodingError(f"instruction is missing {error.args[0]!r}") from error

        if encoding.get("width") != 32:
            raise JsonEncodingError(f"{name}: expected a 32-bit instruction encoding")

        parts = [_parse_part(name, value) for value in values]
        _validate_parts(name, parts)
        exclusions = _encoding_exclusions(instruction.get("condition"))
        parts = _apply_exclusions(name, parts, exclusions)

        return cls(
            name=name,
            mnemonic=_mnemonic(name, instruction.get("assembly")),
            parts=tuple(sorted(parts, key=lambda part: part.hi, reverse=True)),
            features=tuple(_features(instruction.get("condition"))),
            feature_expression=_feature_expression(instruction.get("condition")),
        )

    @property
    def feature(self) -> Optional[str]:
        """The JSON feature requirements in XLAT's historical string form."""
        return self.feature_expression

    def format_encoding(self) -> Tuple[str, str]:
        """Return XLAT's aligned ``pattern`` and ``pfields`` strings."""
        pattern_formats: List[str] = []
        field_formats: List[str] = []
        patterns: List[str] = []
        fields: List[str] = []

        for part in self.parts:
            pattern = part.pattern
            field_width = max(part.width, len(part.field_name), len(pattern))
            pattern_formats.append("{{:<{}}}".format(field_width))
            field_formats.append("{{:^{}}}".format(field_width))
            patterns.append(pattern)
            fields.append(part.field_name)

        return " ".join(pattern_formats).format(*patterns), " ".join(field_formats).format(*fields)

    def render(self, class_prefix: str = "", metadata_only: bool = False) -> str:
        """Render the JSON-derived portion of an XLAT Translation class."""
        pattern, pfields = self.format_encoding()
        decoder = (
            ""
            if metadata_only
            else """
    def decode(self, f):
        return NotImplemented
"""
        )
        unimplemented = "    unimplemented = True\n" if metadata_only else ""
        return f"""
class {class_prefix}{self.name}(Translation):
    pattern = {pattern!r}
    pfields = {pfields!r}
    feature = {self.feature!r}
    asmtemplate = {self.mnemonic!r}
{unimplemented}
{decoder}
"""

    def manifest(self) -> Dict[str, Any]:
        """Return a stable, source-format-independent comparison record."""
        pattern, pfields = self.format_encoding()
        return {
            "name": self.name,
            "mnemonic": self.mnemonic,
            "features": list(self.features),
            "feature_expression": self.feature_expression,
            "pattern": pattern,
            "pfields": pfields,
            "parts": [
                {
                    "hi": part.hi,
                    "lo": part.lo,
                    "bits": part.bits,
                    "excluded": part.excluded,
                    "field": part.field_name,
                }
                for part in self.parts
            ],
        }


def load_a64_encodings(path: Path) -> List[JsonEncoding]:
    """Load every concrete AArch64 instruction encoding from ``Instructions.json``.

    ``path`` may name the JSON file itself or a directory containing it.  The
    Arm document includes A32 and T32 as well; only the A64 InstructionSet is
    selected here.  Do not limit this to leaf nodes: intermediate JSON
    ``Instruction`` nodes are valid encodings too.
    """
    json_path = _instructions_path(path)
    with json_path.open(encoding="utf-8") as source:
        document = json.load(source)

    instruction_set = _find_a64_instruction_set(document.get("instructions"))
    encodings = [
        JsonEncoding.from_instruction(instruction)
        for instruction in _walk_instructions(instruction_set)
        if instruction.get("_type") == "Instruction.Instruction" and instruction.get("encoding")
    ]

    by_name = {encoding.name: encoding for encoding in encodings}
    if len(by_name) != len(encodings):
        raise JsonEncodingError("A64 JSON contains duplicate instruction encoding names")
    return [by_name[name] for name in sorted(by_name)]


def write_manifest(path: Path, encodings: Sequence[JsonEncoding]) -> None:
    """Write deterministic JSON metadata for generated collateral checks."""
    with path.open("w", encoding="utf-8") as output:
        json.dump([encoding.manifest() for encoding in encodings], output, indent=2, sort_keys=True)
        output.write("\n")


def _instructions_path(path: Path) -> Path:
    if path.is_dir():
        path = path / "Instructions.json"
    if path.name != "Instructions.json":
        raise JsonEncodingError(f"expected Instructions.json, got {path}")
    if not path.is_file():
        raise JsonEncodingError(f"cannot read {path}")
    return path


def _find_a64_instruction_set(instructions: Any) -> JsonObject:
    if not isinstance(instructions, list):
        raise JsonEncodingError("top-level instructions is not a list")
    for instruction_set in instructions:
        if (
            isinstance(instruction_set, dict)
            and instruction_set.get("_type") == "Instruction.InstructionSet"
            and instruction_set.get("name") == "A64"
        ):
            return instruction_set
    raise JsonEncodingError("the document does not contain an A64 InstructionSet")


def _walk_instructions(node: JsonObject) -> Iterable[JsonObject]:
    if node.get("_type") == "Instruction.Instruction":
        yield node
    for child in node.get("children", []):
        yield from _walk_instructions(child)


def _parse_part(name: str, value: JsonObject) -> EncodingPart:
    try:
        bit_range = value["range"]
        lo = bit_range["start"]
        width = bit_range["width"]
        encoded_value = _bit_string(value["value"])
        should_be_mask = _bit_string(value["should_be_mask"])
    except KeyError as error:
        raise JsonEncodingError(f"{name}: encoding part is missing {error.args[0]!r}") from error

    if len(encoded_value) != width or len(should_be_mask) != width:
        raise JsonEncodingError(f"{name}: encoding value width does not match its range")

    bits = "".join(
        "-" if mask_bit == "1" else _normalize_bit(value_bit)
        for value_bit, mask_bit in zip(encoded_value, should_be_mask)
    )
    return EncodingPart(
        hi=lo + width - 1,
        width=width,
        bits=bits,
        field_name=value.get("name", ""),
    )


def _bit_string(value: JsonObject) -> str:
    try:
        string = value["value"]
    except KeyError as error:
        raise JsonEncodingError("encoding value has no value string") from error
    if not isinstance(string, str) or len(string) < 2 or string[0] != "'" or string[-1] != "'":
        raise JsonEncodingError(f"unexpected bit string {string!r}")
    return string[1:-1]


def _normalize_bit(bit: str) -> str:
    if bit in "01":
        return bit
    return "-"


def _validate_parts(name: str, parts: Sequence[EncodingPart]) -> None:
    covered_bits = set()
    for part in parts:
        if part.width <= 0 or part.lo < 0 or part.hi >= 32:
            raise JsonEncodingError(f"{name}: invalid range [{part.hi}:{part.lo}]")
        for bit in range(part.lo, part.hi + 1):
            if bit in covered_bits:
                raise JsonEncodingError(f"{name}: overlapping encoding ranges at bit {bit}")
            covered_bits.add(bit)
    if covered_bits != set(range(32)):
        raise JsonEncodingError(f"{name}: encoding does not cover all 32 bits")


def _encoding_exclusions(condition: Any) -> Dict[str, str]:
    """Extract JSON conditions that change the instruction's binary pattern.

    The JSON also stores feature checks and semantic constraints in this AST.
    XLAT can represent direct ``field != bits`` checks, and a negated
    single-value membership test, but the other condition forms do not change
    the decoder pattern and are intentionally left to the source metadata.
    """
    exclusions: Dict[str, str] = {}

    def add(field: str, excluded: str) -> None:
        normalized = "".join(_normalize_bit(bit) for bit in excluded)
        previous = exclusions.setdefault(field, normalized)
        if previous != normalized:
            raise JsonEncodingError(f"conflicting exclusions for field {field}")

    def visit(node: Any) -> None:
        if not isinstance(node, dict):
            return
        node_type = node.get("_type")
        if node_type == "AST.BinaryOp":
            field = _identifier(node.get("left"))
            if node.get("op") == "!=" and field:
                value = _values_value(node.get("right"))
                if value is not None:
                    add(field, value)
                    return
            visit(node.get("left"))
            visit(node.get("right"))
        elif node_type == "AST.UnaryOp" and node.get("op") == "!":
            expression = node.get("expr")
            if isinstance(expression, dict) and expression.get("_type") == "AST.BinaryOp":
                field = _identifier(expression.get("left"))
                values = _single_set_value(expression.get("right"))
                if expression.get("op") == "IN" and field and values is not None:
                    add(field, values)
                    return
            visit(expression)
        else:
            for child in node.values():
                if isinstance(child, (dict, list)):
                    visit(child)

    visit(condition)
    return exclusions


def _apply_exclusions(name: str, parts: Sequence[EncodingPart], exclusions: Mapping[str, str]) -> List[EncodingPart]:
    by_field: Dict[str, List[EncodingPart]] = {}
    for part in parts:
        if part.field_name:
            by_field.setdefault(part.field_name, []).append(part)

    for field, excluded in exclusions.items():
        field_parts = by_field.get(field, [])
        if len(field_parts) != 1:
            raise JsonEncodingError(f"{name}: cannot apply exclusion to field {field!r}")
        if len(excluded) != field_parts[0].width:
            raise JsonEncodingError(f"{name}: exclusion width does not match field {field!r}")

    return [
        EncodingPart(
            hi=part.hi,
            width=part.width,
            bits=part.bits,
            field_name=part.field_name,
            excluded=exclusions.get(part.field_name),
        )
        for part in parts
    ]


def _identifier(node: Any) -> Optional[str]:
    if isinstance(node, dict) and node.get("_type") == "AST.Identifier":
        return node.get("value")
    return None


def _values_value(node: Any) -> Optional[str]:
    if not isinstance(node, dict) or node.get("_type") != "Values.Value":
        return None
    return _bit_string(node)


def _single_set_value(node: Any) -> Optional[str]:
    if not isinstance(node, dict) or node.get("_type") != "AST.Set":
        return None
    values = node.get("values")
    if not isinstance(values, list) or len(values) != 1:
        return None
    return _values_value(values[0])


def _features(condition: Any) -> Iterable[str]:
    features: List[str] = []

    def visit(node: Any) -> None:
        if isinstance(node, dict):
            if node.get("_type") == "AST.Function" and node.get("name") == "IsFeatureImplemented":
                for argument in node.get("arguments", []):
                    feature = _identifier(argument)
                    if feature and feature not in features:
                        features.append(feature)
            for child in node.values():
                if isinstance(child, (dict, list)):
                    visit(child)
        elif isinstance(node, list):
            for child in node:
                visit(child)

    visit(condition)
    return features


def _feature_expression(condition: Any) -> Optional[str]:
    """Render a JSON condition if it carries an architectural feature check.

    A condition can contain a field-dependent exception to a feature
    requirement, for example ``FEAT_SME2 && (sz == '0' || FEAT_SME_I16I64)``.
    Dropping the non-feature branch would strengthen the condition and lose a
    JSON-authored fact, even though XLAT itself does not consume this metadata.
    Conditions that contain no feature predicate are represented exclusively
    by ``pattern`` and are omitted here.
    """

    def selected(node: Any) -> Optional[Tuple[str, int]]:
        if not isinstance(node, dict):
            return None
        if node.get("_type") == "AST.Function" and node.get("name") == "IsFeatureImplemented":
            return _render_condition(node), 4
        if node.get("_type") != "AST.BinaryOp":
            return None

        operator = node.get("op")
        left = selected(node.get("left"))
        right = selected(node.get("right"))
        if operator == "&&":
            if left is None:
                return right
            if right is None:
                return left
            return _join_condition(left, operator, right), 2
        if operator == "||" and (left is not None or right is not None):
            # A non-feature side of an OR is an exception to a feature
            # requirement, so it must remain in the expression.
            return _render_condition(node), 1
        return None

    result = selected(condition)
    return result[0] if result else None


def _join_condition(left: Tuple[str, int], operator: str, right: Tuple[str, int]) -> str:
    precedence = {"||": 1, "&&": 2}[operator]
    left_text = f"({left[0]})" if left[1] < precedence else left[0]
    right_text = f"({right[0]})" if right[1] < precedence else right[0]
    return f"{left_text} {operator} {right_text}"


def _render_condition(node: Any, parent_precedence: int = 0) -> str:
    if not isinstance(node, dict):
        raise JsonEncodingError(f"cannot render condition node {node!r}")

    node_type = node.get("_type")
    if node_type == "AST.Identifier":
        return node["value"]
    if node_type == "Values.Value":
        return node["value"]
    if node_type == "AST.Integer":
        return str(node["value"])
    if node_type == "AST.Bool":
        return "TRUE" if node["value"] else "FALSE"
    if node_type == "AST.Set":
        return "{" + ", ".join(_render_condition(value) for value in node["values"]) + "}"
    if node_type == "AST.Function":
        arguments = ", ".join(_render_condition(argument) for argument in node.get("arguments", []))
        if node.get("name") == "IsFeatureImplemented":
            return arguments
        return f"{node['name']}({arguments})"
    if node_type == "AST.UnaryOp":
        return f"{node['op']}({_render_condition(node['expr'])})"
    if node_type == "AST.BinaryOp":
        precedence = {"||": 1, "&&": 2, "==": 3, "!=": 3, "IN": 3}.get(node["op"], 3)
        left = _render_condition(node["left"])
        right = _render_condition(node["right"])
        for child, text_name in ((node["left"], "left"), (node["right"], "right")):
            if not isinstance(child, dict) or child.get("_type") != "AST.BinaryOp":
                continue
            child_operator = child.get("op")
            child_precedence = {"||": 1, "&&": 2, "==": 3, "!=": 3, "IN": 3}.get(child_operator, 3)
            logical_child = child_operator in {"&&", "||"}
            if logical_child and (child_operator != node["op"] or child_precedence < precedence):
                if text_name == "left":
                    left = f"({left})"
                else:
                    right = f"({right})"
        text = f"{left} {node['op']} {right}"
        return f"({text})" if precedence < parent_precedence else text
    raise JsonEncodingError(f"cannot render condition type {node_type!r}")


def _mnemonic(name: str, assembly: Any) -> str:
    if not isinstance(assembly, dict):
        raise JsonEncodingError(f"{name}: instruction has no assembly metadata")
    for symbol in assembly.get("symbols", []):
        if symbol.get("_type") == "Instruction.Symbols.Literal":
            mnemonic = symbol.get("value")
            if isinstance(mnemonic, str) and mnemonic:
                return mnemonic
    raise JsonEncodingError(f"{name}: assembly metadata has no literal mnemonic")
