# Copyright (c) 2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Read AARCHMRS instruction data used by the ADIS generator.

The reader deliberately keeps Arm's machine-readable representation separate
from ADIS's generated C++ representation.  In particular, conditional
encoding constraints are normalized here so that every later code-generation
step has one source of truth for an instruction's legal bit patterns.
"""

from __future__ import annotations

import copy
import gzip
import itertools
import json
import re
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Any, Iterable

import encoding
import field
import instr

JsonObject = dict[str, Any]


def _load_document(path: Path) -> JsonObject:
    """Load an AARCHMRS JSON document, including a gzip-compressed source."""

    if path.suffix == ".gz":
        with gzip.open(path, mode="rt", encoding="utf-8") as source:
            return json.load(source)
    with path.open(encoding="utf-8") as source:
        return json.load(source)


@dataclass(frozen=True)
class BitRange:
    """A contiguous instruction bit range, numbered as in AARCHMRS.

    AARCHMRS records ``start`` as the low-order bit.  Its encoding entries
    remain ordered from high to low, but consumers must not mistake
    ``start`` for the high-order bit.
    """

    start: int
    width: int

    @property
    def low(self) -> int:
        return self.start

    @property
    def high(self) -> int:
        return self.start + self.width - 1


@dataclass(frozen=True)
class BitField:
    """One named or anonymous part of an instruction encoding."""

    name: str | None
    bit_range: BitRange
    value: str


@dataclass(frozen=True)
class Constraint:
    """A field constraint represented by an instruction condition."""

    field: str
    operator: str
    value: str


@dataclass(frozen=True)
class Encoding:
    """A concrete A64 instruction form and its machine-readable metadata."""

    name: str
    operation_id: str
    fields: tuple[BitField, ...]
    conditions: tuple[tuple[Constraint, ...], ...]
    assembly: JsonObject
    encoded_in: dict[str, tuple[str, ...]]
    raw: JsonObject


def _bit_string(value: str) -> str:
    """Convert an AARCHMRS quoted bit string into ADIS ternary notation."""

    if len(value) < 2 or value[0] != "'" or value[-1] != "'":
        raise ValueError(f"Expected a quoted bit string, got {value!r}")
    bits = value[1:-1]
    if not bits or any(bit not in "01x" for bit in bits):
        raise ValueError(f"Expected binary bits, got {value!r}")
    return bits.replace("x", "-")


def _identifier_name(node: JsonObject) -> str | None:
    if node.get("_type") == "AST.Identifier":
        return node["value"]
    return None


def _value_bits(node: JsonObject) -> str | None:
    if node.get("_type") == "Values.Value":
        return _bit_string(node["value"])
    return None


def _combine_and(
    left: Iterable[tuple[Constraint, ...]], right: Iterable[tuple[Constraint, ...]]
) -> list[tuple[Constraint, ...]]:
    return [left_part + right_part for left_part, right_part in itertools.product(left, right)]


def _conditions(node: JsonObject | None) -> list[tuple[Constraint, ...]]:
    """Return field-only alternatives from an AARCHMRS condition AST.

    Feature tests affect architectural availability but not instruction-bit
    selection, so they intentionally contribute no bit constraint.  The
    caller retains the complete AST in ``Encoding.raw`` for later feature
    handling.
    """

    if node is None:
        return [()]

    kind = node.get("_type")
    if kind == "AST.Bool":
        return [()] if node["value"] else []

    if kind == "AST.Function":
        return [()]

    if kind == "AST.UnaryOp":
        if node["op"] != "!":
            raise ValueError(f"Unsupported unary condition: {node!r}")
        return _negated_conditions(node["expr"])

    if kind != "AST.BinaryOp":
        raise ValueError(f"Unsupported condition node: {node!r}")

    operator = node["op"]
    if operator == "&&":
        return _combine_and(_conditions(node["left"]), _conditions(node["right"]))
    if operator == "||":
        return _conditions(node["left"]) + _conditions(node["right"])

    field = _identifier_name(node["left"])
    if field is None:
        # Function-valued comparisons, such as system-operation category
        # tests, do not add an independent instruction-bit constraint.
        return [()]

    if operator in {"==", "!="}:
        value = _value_bits(node["right"])
        if value is None:
            return [()]
        constraint_operator = "eq" if operator == "==" else "ne"
        return [(Constraint(field, constraint_operator, value),)]

    if operator == "IN" and node["right"].get("_type") == "AST.Set":
        values = []
        for item in node["right"]["values"]:
            value = _value_bits(item)
            if value is None:
                raise ValueError(f"Unsupported set member: {item!r}")
            values.append((Constraint(field, "eq", value),))
        return values

    raise ValueError(f"Unsupported condition operator: {node!r}")


def _negated_conditions(node: JsonObject | None) -> list[tuple[Constraint, ...]]:
    """Return bit-pattern alternatives for the negation of ``node``."""

    if node is None:
        return []
    kind = node.get("_type")
    if kind == "AST.Bool":
        return [] if node["value"] else [()]
    if kind == "AST.Function":
        # Feature availability does not select instruction bits.
        return [()]
    if kind == "AST.UnaryOp":
        if node["op"] != "!":
            raise ValueError(f"Unsupported unary condition: {node!r}")
        return _conditions(node["expr"])
    if kind != "AST.BinaryOp":
        raise ValueError(f"Unsupported condition node: {node!r}")

    operator = node["op"]
    if operator == "&&":
        return _negated_conditions(node["left"]) + _negated_conditions(node["right"])
    if operator == "||":
        return _combine_and(_negated_conditions(node["left"]), _negated_conditions(node["right"]))

    field = _identifier_name(node["left"])
    if field is None:
        return [()]
    if operator in {"==", "!="}:
        value = _value_bits(node["right"])
        if value is None:
            return [()]
        constraint_operator = "ne" if operator == "==" else "eq"
        return [(Constraint(field, constraint_operator, value),)]
    if operator == "IN" and node["right"].get("_type") == "AST.Set":
        constraints = []
        for item in node["right"]["values"]:
            value = _value_bits(item)
            if value is None:
                raise ValueError(f"Unsupported set member: {item!r}")
            constraints.append(Constraint(field, "ne", value))
        return [tuple(constraints)]
    raise ValueError(f"Unsupported condition operator: {node!r}")


def _encoded_in(raw: JsonObject) -> dict[str, tuple[str, ...]]:
    metadata = raw.get("_meta") or {}
    result = {}
    for displayed, fields in metadata.get("encoded_in", {}).items():
        identifiers = []
        for source_field in fields:
            identifier = _identifier_name(source_field)
            if identifier is None:
                raise ValueError(f"Unsupported encoded_in member: {source_field!r}")
            identifiers.append(identifier)
        result[displayed] = tuple(identifiers)
    return result


def _walk_instructions(node: JsonObject) -> Iterable[JsonObject]:
    if node.get("_type") == "Instruction.Instruction":
        yield node
    for child in node.get("children", []):
        yield from _walk_instructions(child)


def _and(left: JsonObject | None, right: JsonObject | None) -> JsonObject | None:
    """Combine two optional condition trees without discarding either."""

    if left is None:
        return copy.deepcopy(right)
    if right is None:
        return copy.deepcopy(left)
    return {"_type": "AST.BinaryOp", "left": copy.deepcopy(left), "op": "&&", "right": copy.deepcopy(right)}


def _record_from_raw(raw: JsonObject) -> Encoding:
    condition_alternatives = _conditions(raw.get("condition"))
    conditions = tuple(dict.fromkeys(tuple(dict.fromkeys(alternative)) for alternative in condition_alternatives))
    return Encoding(
        name=raw["name"],
        operation_id=raw["operation_id"],
        fields=_read_fields(raw),
        conditions=conditions,
        assembly=raw["assembly"],
        encoded_in=_encoded_in(raw),
        raw=raw,
    )


def _read_fields(raw: JsonObject) -> tuple[BitField, ...]:
    fields = []
    for item in raw["encoding"]["values"]:
        bit_range = BitRange(item["range"]["start"], item["range"]["width"])
        value = _bit_string(item["value"]["value"])
        if len(value) != bit_range.width:
            raise ValueError(f"Invalid bit width in {raw['name']}: {item!r}")
        fields.append(BitField(item.get("name"), bit_range, value))

    fields.sort(key=lambda item: item.bit_range.high, reverse=True)
    total_width = sum(item.bit_range.width for item in fields)
    if raw["encoding"]["width"] != total_width:
        raise ValueError(f"Non-contiguous encoding in {raw['name']}")
    if raw["encoding"]["width"] != 32:
        raise ValueError(f"ADIS only supports 32-bit A64 encodings: {raw['name']}")
    return tuple(fields)


def read_a64(path: Path) -> list[Encoding]:
    """Read every concrete A64 instruction form from ``Instructions.json``."""

    document = _load_document(path)

    version = document.get("_meta", {}).get("version", {})
    if version.get("ref") != "2026-09_rel":
        raise ValueError(f"Unexpected AARCHMRS release: {version!r}")

    instruction_sets = document["instructions"]
    a64 = next(item for item in instruction_sets if item.get("name") == "A64")
    result = []
    for raw in _walk_instructions(a64):
        result.append(_record_from_raw(raw))
    return result


def _with_constraints(record: Encoding, constraints: Iterable[Constraint]) -> Encoding:
    """Return a record with an additional conjunction of bit constraints."""

    extra = tuple(constraints)
    return replace(record, conditions=tuple(alternative + extra for alternative in record.conditions))


def _choice_constraints(record: Encoding, display: str, index: int) -> tuple[Constraint, ...]:
    """Map an assembly-choice index to the fields that select that choice.

    The JSON grammar associates a displayed choice with its encoding fields.
    Its alternatives are ordered by the unsigned concatenation of those
    fields, most-significant field first.  This is the same relationship the
    architecture grammar uses for suffixes and operand spellings.
    """

    names = record.encoded_in[display]
    fields = {item.name: item for item in record.fields if item.name}
    widths = [fields[name].bit_range.width for name in names]
    value = f"{index:0{sum(widths)}b}"
    offset = 0
    result = []
    for name, width in zip(names, widths):
        result.append(Constraint(name, "eq", value[offset : offset + width]))
        offset += width
    return tuple(result)


def _choice_text(rules: dict[str, JsonObject], assembly: JsonObject) -> str:
    """Return the literal spelling of one grammar-choice alternative."""

    tokens = _assembly_tokens(rules, {}, assembly, set())
    if any(displayed is not None for _, displayed, _ in tokens):
        raise ValueError(f"Choice unexpectedly contains an encoded operand: {assembly!r}")
    return "".join(text for text, _, _ in tokens)


def _replace_rule(assembly: JsonObject, rule_id: str, text: str) -> JsonObject:
    """Replace one grammar rule reference with an explicit literal spelling."""

    result = copy.deepcopy(assembly)
    result["symbols"] = [
        {"_type": "Instruction.Symbols.Literal", "value": text}
        if symbol.get("_type") == "Instruction.Symbols.RuleReference" and symbol["rule_id"] == rule_id
        else symbol
        for symbol in result["symbols"]
    ]
    return result


def _assembly_mnemonic(assembly: JsonObject) -> str:
    """Get the initial literal mnemonic from an assembly grammar."""

    for symbol in assembly.get("symbols", []):
        if symbol.get("_type") == "Instruction.Symbols.Literal":
            return symbol["value"]
    raise ValueError(f"Assembly has no literal mnemonic: {assembly!r}")


def _expand_mnemonic_choices(record: Encoding, rules: dict[str, JsonObject]) -> list[Encoding]:
    """Split the small grammar choices that change an ADIS encoding name.

    Most choices are operand formatting and remain dynamic.  ADIS has always
    represented the vector ``2`` suffix and BF16 bottom/top spellings as
    separate decoder entries, so retain that interface while deriving their
    bit selection from the grammar.
    """

    assembly = record.assembly
    for symbol in assembly.get("symbols", []):
        if symbol.get("_type") != "Instruction.Symbols.RuleReference":
            continue
        rule_id = symbol["rule_id"]
        rule = rules[rule_id]
        display = rule.get("display")
        if display not in {"2", "<bt>"} or display not in record.encoded_in:
            continue
        if rule["_type"] != "Instruction.Rules.Choice":
            raise ValueError(f"Expected choice rule for {record.name}: {rule_id}")

        expanded = []
        for index, choice in enumerate(rule["choices"]):
            text = _choice_text(rules, choice)
            candidate = _with_constraints(record, _choice_constraints(record, display, index))
            candidate = replace(candidate, assembly=_replace_rule(assembly, rule_id, text))
            if display == "2":
                candidate = replace(
                    candidate,
                    name=(record.name if not text else record.name.replace("_asimd", "2_asimd", 1)),
                )
            else:
                candidate = replace(
                    candidate,
                    name=record.name.replace("_asimd", f"{text}_asimd", 1),
                )
            expanded.append(candidate)
        return expanded
    return [record]


def _alias_records(record: Encoding) -> Iterable[Encoding]:
    """Yield aliases declared below a concrete JSON instruction form."""

    for alias in record.raw.get("children", []):
        if alias.get("_type") != "Instruction.InstructionAlias":
            continue
        raw = copy.deepcopy(record.raw)
        raw["name"] = f"{record.name}_{alias['name']}"
        raw["operation_id"] = alias["operation_id"]
        raw["assembly"] = alias["assembly"]
        raw["_meta"] = alias.get("_meta", {})
        raw["condition"] = _and(record.raw.get("condition"), alias.get("condition"))
        raw["children"] = []
        yield _record_from_raw(raw)


def _expand_system_aliases(record: Encoding, rules: dict[str, JsonObject]) -> list[tuple[Encoding, str]]:
    """Specialize JSON system-operation aliases into ADIS decoder entries.

    System-operation spelling tables are expressed as assembly-rule choices in
    AARCHMRS.  Their rule identifiers carry the field values and the terminal
    grammar rule supplies the printed operation name.  Reading those two JSON
    fields replaces ADIS's former handwritten operation table.
    """

    assembly = record.assembly
    mnemonic = _assembly_mnemonic(assembly)
    result = []
    for symbol in assembly.get("symbols", []):
        if symbol.get("_type") != "Instruction.Symbols.RuleReference":
            continue
        rule_id = symbol["rule_id"]
        rule = rules[rule_id]
        display = rule.get("display")
        if not display or not display.endswith("_op>") or display not in record.encoded_in:
            continue
        if rule["_type"] != "Instruction.Rules.Choice":
            raise ValueError(f"Expected system-operation choice: {record.name} {rule_id}")
        field_count = len(record.encoded_in[display])
        for choice in rule["choices"]:
            reference = choice.get("symbols", [{}])[0]
            if reference.get("_type") != "Instruction.Symbols.RuleReference":
                raise ValueError(f"Unexpected system-operation choice: {choice!r}")
            encoded = reference["rule_id"]
            # The JSON rule identifier is descriptive rather than a schema
            # field; a few spellings capitalize its prefix differently from
            # the reference.  The binary chunks are the stable data-bearing
            # portion and appear in operand-field order.
            values = [piece for piece in encoded.split("_") if piece and not (set(piece) - {"0", "1", "x"})]
            if len(values) != field_count or any(not value or set(value) - {"0", "1", "x"} for value in values):
                raise ValueError(f"Cannot recover system-operation fields from {encoded!r}")
            operation = _choice_text(rules, choice)
            if not operation:
                raise ValueError(f"System-operation choice has no spelling: {encoded!r}")
            fields = {bit_field.name: bit_field for bit_field in record.fields if bit_field.name}
            constraints = []
            for field_name, value in zip(record.encoded_in[display], values):
                width = fields[field_name].bit_range.width
                if len(value) > width:
                    raise ValueError(f"System-operation field {field_name!r} is too narrow for {encoded!r}")
                # The choice rule names the bits which vary beneath the
                # alias's broader condition.  Those are the field's
                # low-order bits: e.g., AT's ``CRm == '100x'`` condition
                # combines with the rule fragment ``0`` to select ``1000``.
                constraints.append(Constraint(field_name, "eq", _bit_string(f"'{'x' * (width - len(value))}{value}'")))
            candidate = _with_constraints(record, constraints)
            candidate = replace(
                candidate,
                # ``record`` is the JSON alias (for example ``..._DC``),
                # so its name already carries the base mnemonic.
                name=f"{record.name}_{operation}",
                assembly=_replace_rule(assembly, rule_id, operation),
            )
            result.append((candidate, f"{mnemonic}_{operation}"))
        return result
    return [(record, "INVALID")]


def _fixed_system_alias_variants(record: Encoding) -> list[Encoding]:
    """Offer a stable ADIS name for a fixed two-word system spelling.

    A few JSON aliases spell their operation as a literal (for example
    ``CFP RCTX``) rather than as a choice rule.  The legacy ADIS API exposed
    that literal in the encoding identifier.  Keep both the concise JSON
    name and that API-compatible spelling; the supported-entry policy below
    selects the one that belongs in a generated decoder.
    """

    if not record.name.startswith(("SYS_", "SYSL_", "SYSP_")):
        return [record]
    literals = [
        symbol["value"]
        for symbol in record.assembly.get("symbols", [])
        if symbol.get("_type") == "Instruction.Symbols.Literal"
    ]
    if len(literals) < 2 or literals[1] != "RCTX":
        return [record]
    return [record, replace(record, name=f"{record.name}_{literals[1]}")]


class _source_record:
    """Minimal instruction metadata used by ADIS's generated properties."""

    def __init__(self, name: str, operation_id: str):
        self.name = name
        self.operation_id = operation_id

    def get(self, key: str) -> str | None:
        if key == "id":
            return self.operation_id
        if key == "title":
            return self.name
        return None

    def findall(self, _path: str) -> list[object]:
        return []


def _features(node: Any) -> set[str]:
    if isinstance(node, dict):
        if node.get("_type") == "AST.Function" and node.get("name") == "IsFeatureImplemented":
            return {
                identifier
                for argument in node.get("arguments", [])
                if (identifier := _identifier_name(argument)) is not None
            }
        return set().union(*(_features(value) for value in node.values()))
    if isinstance(node, list):
        return set().union(*(_features(value) for value in node))
    return set()


def _primary_feature(raw: JsonObject) -> str:
    features = _features(raw.get("condition"))
    if not features:
        return "unspecified"
    # An instruction which is also available in streaming mode remains an
    # SVE decoder entry.  This applies to every SVE revision, not just the
    # original SVE and SVE2 feature names.
    sve_features = sorted(feature for feature in features if feature.startswith("FEAT_SVE"))
    if sve_features:
        return sve_features[0]

    family_priority = ("FEAT_AdvSIMD", "FEAT_FP", "FEAT_SME2", "FEAT_SME")
    for feature in family_priority:
        if feature in features:
            return feature
    return sorted(features)[0]


def _implemented_names(path: Path) -> set[str]:
    """Load ADIS's deliberate implementation boundary for this release."""

    policy_path = path.with_name("implemented-encodings.json")
    with policy_path.open() as source:
        policy = json.load(source)
    if policy.get("aarchmrs_release") != "2026-09_rel":
        raise ValueError(f"Unexpected ADIS implementation policy: {policy_path}")
    names = policy.get("encodings")
    if not isinstance(names, list) or any(not isinstance(name, str) for name in names):
        raise ValueError(f"Invalid ADIS implementation policy: {policy_path}")
    if len(names) != len(set(names)):
        raise ValueError(f"Duplicate ADIS implementation policy entry: {policy_path}")
    return set(names)


def _instruction_class(feature: str) -> str:
    if feature.startswith("FEAT_SME2"):
        return "sme2"
    if feature.startswith("FEAT_SME"):
        return "sme"
    if feature.startswith("FEAT_SVE2p1"):
        return "sve2p1"
    if feature.startswith("FEAT_SVE2"):
        return "sve2"
    if feature.startswith("FEAT_SVE"):
        return "sve"
    if feature == "FEAT_AdvSIMD":
        return "advsimd"
    if feature == "FEAT_FP":
        return "float"
    return "general"


_ATOMIC_OPERATION_PREFIXES = (
    "CAS",
    "LDBF",
    "LDADD",
    "LDCLR",
    "LDEOR",
    "LDSET",
    "LDSMAX",
    "LDSMIN",
    "LDUMAX",
    "LDUMIN",
    "RCW",
    "STBF",
    "STF",
    "SWP",
)


def _is_atomic_operation(operation: str) -> bool:
    """Return whether a machine-readable operation is an atomic RMW.

    The operation namespace has ordinary loads whose names begin with LDF
    and LDT (for example LDFF1 and LDTR).  Match the actual RMW families so
    those ordinary loads remain visible to Panthera as loads.
    """

    return operation.startswith(_ATOMIC_OPERATION_PREFIXES) or operation.startswith(
        ("LDFADD", "LDFMAX", "LDFMIN", "LDTADD", "LDTCLR", "LDTSET")
    )


def _memory_element_size(mnemonic: str) -> int:
    """Return the byte/halfword/word/doubleword size encoded in a mnemonic.

    SVE memory operation names end in their memory element designator.  This
    is the same information the former generator obtained from the input's
    operational pseudocode, but is available directly from the JSON
    assembly grammar.  Returning zero preserves the existing dynamic
    decoding path for forms without a fixed size.
    """

    for suffix, size in (("Q", 128), ("D", 64), ("W", 32), ("H", 16), ("B", 8)):
        if mnemonic.endswith(suffix):
            return size
    return 0


def _vector_source_count(record: Encoding, feature: str, assembly: instr.asm_instruction_t) -> int:
    """Count scalable-vector source operands from the JSON assembly grammar.

    ADIS uses this only while resolving a MOVPRFX fusion.  AARCHMRS does not
    carry the former presentation-layer read/write annotations, but its
    register spellings retain the required distinction: ``Zd`` is a pure
    destination, ``Zdn``/``Zda`` are read-write accumulators, and an ``Zt``
    list is a source only for a store.  AdvSIMD registers deliberately do not
    participate in this SVE-specific count.
    """

    semantic = _semantic_properties(record, feature)
    count = 0
    bracket_depth = 0
    for operand in assembly.get_operands():
        text = operand.get_text()
        if text == "[":
            bracket_depth += 1
            continue
        if text == "]":
            bracket_depth -= 1
            continue
        if bracket_depth:
            continue
        match = re.fullmatch(r"<(?P<name>Z[a-z][a-zA-Z0-9]*)>", text)
        if match is None:
            continue
        name = match["name"]
        if name.startswith("Zd") and not name.startswith(("Zdn", "Zda")):
            continue
        if name.startswith("Zt") and (semantic["load"] or semantic["gather"]):
            continue
        count += 1

    # LUTI reads one table operand regardless of its architectural register
    # list width; its element-index vector is not a data source.  PMLAL's
    # Zda range is an output accumulator range, while Zn and Zm are its two
    # data inputs.
    operation = record.operation_id.upper()
    if operation.startswith("LUTI"):
        return min(count, 1)
    if operation.startswith("PMLAL"):
        return max(count - 2, 0)
    return count


def _semantic_properties(record: Encoding, feature: str) -> dict[str, bool | str]:
    """Classify ADIS-visible instruction behavior from JSON identifiers.

    AARCHMRS provides an encoding name, operation identifier, mnemonic
    grammar, and feature condition, but deliberately does not duplicate the
    descriptive prose that earlier generator revisions used for this task.
    These rules are therefore ADIS-owned decoder semantics.  They use only
    stable machine-readable instruction names and operation identifiers.
    """

    name = record.name.lower()
    mnemonic = _assembly_mnemonic(record.assembly).upper()
    operation = record.operation_id.upper()
    is_sve = feature.startswith("FEAT_SVE")
    is_atomic = _is_atomic_operation(operation)
    is_prefetch = mnemonic.startswith("PRF")
    is_load = mnemonic.startswith("LD") and not is_atomic
    is_store = mnemonic.startswith("ST") and not is_atomic
    is_gather = is_sve and is_load and any(part in name for part in ("_z_p_ai_", "_z_p_ar_", "_z_p_bz_"))
    is_scatter = is_sve and is_store and any(part in name for part in ("_z_p_ai_", "_z_p_ar_", "_z_p_bz_"))
    is_gather_prefetch = is_sve and is_prefetch and any(part in name for part in ("_p_ai_", "_p_ar_", "_p_bz_"))
    is_load_ff = is_load and "ldff" in name
    is_load_nf = is_load and "ldnf" in name
    is_sve_multi = is_sve and any(mnemonic.startswith(prefix) for prefix in ("LD2", "LD3", "LD4", "ST2", "ST3", "ST4"))
    is_advsimd_multi = feature == "FEAT_AdvSIMD" and any(
        mnemonic.startswith(prefix) for prefix in ("LD1", "LD2", "LD3", "LD4", "ST1", "ST2", "ST3", "ST4")
    )
    is_return = mnemonic.startswith(("RET", "ERET"))
    is_compare_branch = operation.lower().startswith(("cbcc_", "cbbcc_", "cbhcc_"))
    is_branch = "branch" in name or "miscbranch" in name or is_compare_branch
    is_branch_cond = is_branch and ("condbranch" in name or mnemonic.startswith(("B.", "BC.", "CB", "TB")))
    is_branch_call = mnemonic.startswith("BL")
    is_branch_indirect = is_branch and mnemonic != "DRPS" and (is_return or "branch_reg" in name)
    is_mops = feature == "FEAT_MOPS"
    is_mops_set_g = is_mops and mnemonic.startswith("SETG")
    is_mops_post = is_mops and (mnemonic.startswith("CPY") or mnemonic.startswith(("SETP", "SETM", "SETE")))
    is_pre_index = is_mops_set_g or (not is_mops and ("immpre" in name or "_pre" in name or "spre_" in name))
    is_post_index = is_mops_post or (
        not is_mops
        and ("immpost" in name or "spost" in name or "_post" in name or ("writeback" in name and mnemonic == "LDAPR"))
    )
    element_size = _memory_element_size(mnemonic) if is_sve else 0
    may_write_flags = (
        mnemonic.startswith(
            ("ADDS", "SUBS", "ANDS", "BICS", "ADCS", "SBCS", "CCMP", "CCMN", "FCCMP", "FCMP", "CMN", "CMP", "TST")
        )
        or operation.startswith("RCW")
        or (is_mops and mnemonic.startswith(("CPY", "SET")))
        or mnemonic.startswith("WHILE")
        or mnemonic
        in {
            "BRKAS",
            "BRKBS",
            "BRKNS",
            "BRKPAS",
            "BRKPBS",
            "CFINV",
            "AXFLAG",
            "XAFLAG",
            "RMIF",
            "SETF8",
            "SETF16",
            "CTERMEQ",
            "CTERMNE",
            "EORS",
            "FJCVTZS",
            "MATCH",
            "NMATCH",
            "NANDS",
            "NORS",
            "ORNS",
            "ORRS",
            "PFIRST",
            "PNEXT",
            "PTEST",
            "PTRUES",
            "RDFFRS",
            "SUBPS",
        }
    )
    may_read_flags = (
        mnemonic in {"ADC", "ADCS", "SBC", "SBCS"}
        or mnemonic.startswith(("CCMP", "CCMN", "CSEL", "CSINC", "CSINV", "CSNEG", "FCCMP", "FCSEL"))
        or operation in {"B_COND", "BC_COND"}
        or mnemonic in {"CFINV", "AXFLAG", "XAFLAG", "CTERMEQ", "CTERMNE"}
    )

    category = _instruction_class(feature)
    if is_gather_prefetch:
        category = "gather_prefetch"
    elif is_prefetch:
        category = "prefetch"
    elif is_gather:
        category = "gather_ff" if is_load_ff else ("gather_nt" if "nt" in name else "gather")
    elif is_scatter:
        category = "scatter_nt" if "nt" in name else "scatter"
    elif is_atomic:
        category = "atomic"
    elif is_load_ff:
        category = "load_ff"
    elif is_load_nf:
        category = "load_nf"
    elif is_load:
        category = "load_sve_mult" if is_sve_multi else ("load_mult" if is_advsimd_multi else "load")
    elif is_store:
        category = "store_sve_mult" if is_sve_multi else ("store_mult" if is_advsimd_multi else "store")
    elif is_return:
        category = "return"
    elif is_branch:
        category = "branch"
    elif mnemonic.startswith(("CPY", "SET")):
        category = "memcpy"

    return {
        "category": category,
        "load": is_load and not is_gather,
        "load_ff": is_load_ff,
        "load_nf": is_load_nf,
        "store": is_store and not is_scatter,
        "atomic": is_atomic,
        "gather": is_gather,
        "scatter": is_scatter,
        "prefetch": is_prefetch,
        "gather_prefetch": is_gather_prefetch,
        "branch": is_branch,
        "branch_cond": is_branch_cond,
        "branch_indirect": is_branch_indirect,
        "branch_call": is_branch_call,
        "branch_return": is_return,
        "exception": "exception" in name,
        "address_translation": "_at_" in name,
        "eret": mnemonic.startswith("ERET"),
        "sme": feature.startswith("FEAT_SME"),
        "pc64plus": mnemonic in {"B", "BL", "B.", "BC.", "CBZ", "CBNZ", "TBZ", "TBNZ"},
        "pc64minus": False,
        "pre_index": is_pre_index,
        "post_index": is_post_index,
        "memop_esize": element_size,
        "memop_msize": element_size,
        "may_read_flags": may_read_flags,
        "may_write_flags": may_write_flags,
    }


def _make_encoding(record: Encoding, constraints: tuple[Constraint, ...]) -> encoding.encoding_t:
    result = encoding.encoding_t()
    by_name = {}
    for index, source_field in enumerate(record.fields):
        name = source_field.name or f"field{index}"
        target = field.field_t(name, source_field.bit_range.high, source_field.value)
        result.add(target)
        if source_field.name:
            by_name[source_field.name] = target

    for constraint in constraints:
        target = by_name.get(constraint.field)
        if target is None:
            raise ValueError(f"{record.name}: condition references {constraint.field!r} without an encoding field")
        if len(constraint.value) != target.len():
            raise ValueError(f"{record.name}: invalid condition width for {constraint.field}")
        if constraint.operator == "eq":
            merged = "".join(
                source if source != "-" else condition for source, condition in zip(target.ternbits, constraint.value)
            )
            if any(
                source != "-" and source != condition for source, condition in zip(target.ternbits, constraint.value)
            ):
                raise ValueError(f"{record.name}: contradictory condition on {constraint.field}")
            target.ternbits = merged
        elif constraint.operator == "ne":
            if target.ternbits != "-" * target.len():
                raise ValueError(f"{record.name}: cannot apply inequality to constrained {constraint.field}")
            target.ternbits = constraint.value
            target.forbid = True
        else:
            raise ValueError(f"{record.name}: unsupported condition operator {constraint.operator!r}")
    return result


def _select_constraints(record: Encoding) -> tuple[Constraint, ...]:
    """Choose the most specific source-level availability alternative.

    A feature-only alternative can make a field restriction optional on a
    newer architecture revision.  Selecting the most specific alternative
    keeps the generated decoder conservative until that newer form is
    explicitly implemented.
    """

    return max(record.conditions, key=len)


def _matches(value: str, pattern: str) -> bool:
    return all(pattern_bit == "-" or value_bit == pattern_bit for value_bit, pattern_bit in zip(value, pattern))


def _a64_legality_constraints(record: Encoding) -> tuple[Constraint, ...]:
    """Supply architectural constraints omitted from the encoding bit layout."""

    if record.name in {"SBFM_32M_bitfield", "BFM_32M_bitfield", "UBFM_32M_bitfield"}:
        # The 32-bit bitfield forms encode only five-bit rotation and mask
        # values, even though their shared fields are six bits wide.
        return (Constraint("immr", "eq", "0-----"), Constraint("imms", "eq", "0-----"))

    if "comswappr" in record.name:
        # Compare-and-swap pair forms name the first register in each pair;
        # those starts are necessarily even numbered.
        return (Constraint("Rs", "eq", "----0"), Constraint("Rt", "eq", "----0"))

    return ()


def encoding_patterns(record: Encoding) -> tuple[str, ...]:
    """Expand conditional field tests into decoder-safe ternary patterns."""

    base = "".join(source_field.value for source_field in record.fields)
    by_name = {source_field.name: source_field for source_field in record.fields if source_field.name}
    patterns = set()

    for alternative in record.conditions:
        grouped: dict[str, list[Constraint]] = {}
        for constraint in (*alternative, *_a64_legality_constraints(record)):
            grouped.setdefault(constraint.field, []).append(constraint)
        unknown = set(grouped) - set(by_name)
        if unknown:
            raise ValueError(f"{record.name}: condition references unknown fields {sorted(unknown)}")

        options = []
        for name, constraints in grouped.items():
            source_field = by_name[name]
            values = []
            for value in range(1 << source_field.bit_range.width):
                bits = f"{value:0{source_field.bit_range.width}b}"
                if not _matches(bits, source_field.value):
                    continue
                if all(
                    (
                        _matches(bits, constraint.value)
                        if constraint.operator == "eq"
                        else not _matches(bits, constraint.value)
                    )
                    for constraint in constraints
                ):
                    values.append(bits)
            options.append((source_field, values))

        for replacements in itertools.product(*(values for _, values in options)) if options else [()]:
            result = list(base)
            for (source_field, _), replacement in zip(options, replacements):
                offset = 31 - source_field.bit_range.high
                result[offset : offset + source_field.bit_range.width] = replacement
            patterns.add("".join(result))
    return tuple(sorted(patterns))


def _rule_tokens(
    rules: dict[str, JsonObject], encoded_in: dict[str, tuple[str, ...]], rule_id: str, stack: set[str]
) -> list[tuple[str, str | None, str | None]]:
    if rule_id in stack:
        raise ValueError(f"Recursive assembly rule: {rule_id}")
    rule = rules[rule_id]
    display = rule.get("display")
    if display in encoded_in:
        # Keep the originating rule with the token.  Multiple grammar rules
        # often have the same display text (notably ``<imm>``), but can have
        # entirely different encodings.  A lookup table is valid only for the
        # particular rule used by this instruction.
        return [(display, display, rule_id)]
    if rule["_type"] == "Instruction.Rules.Token":
        if rule_id == "SPACE":
            return [("  ", None, None)]
        if rule_id == "COMMA":
            return [(",", None, None)]
        return [(rule["default"], None, None)] if rule["default"] else []
    if display and not display.startswith("<"):
        return [(display, None, None)]

    new_stack = {*stack, rule_id}
    if rule["_type"] == "Instruction.Rules.Rule":
        symbols = rule.get("symbols")
        return _assembly_tokens(rules, encoded_in, symbols, new_stack) if symbols else []
    if rule["_type"] == "Instruction.Rules.Choice":
        # The first nonempty choice is the default spelling.  Rules whose
        # display maps to encoded fields were handled above and are emitted as
        # data-dependent operands by the generator.
        for choice in rule["choices"]:
            if choice:
                return _assembly_tokens(rules, encoded_in, choice, new_stack)
        return []
    raise ValueError(f"Unsupported assembly rule {rule_id}: {rule!r}")


def _assembly_tokens(
    rules: dict[str, JsonObject], encoded_in: dict[str, tuple[str, ...]], assembly: JsonObject | None, stack: set[str]
) -> list[tuple[str, str | None, str | None]]:
    if assembly is None:
        return []
    result = []
    for symbol in assembly.get("symbols", []):
        if symbol["_type"] == "Instruction.Symbols.Literal":
            result.append((symbol["value"], None, None))
        elif symbol["_type"] == "Instruction.Symbols.RuleReference":
            result.extend(_rule_tokens(rules, encoded_in, symbol["rule_id"], stack))
        else:
            raise ValueError(f"Unsupported assembly symbol: {symbol!r}")
    return result


def _make_assembly(
    record: Encoding, rules: dict[str, JsonObject]
) -> tuple[instr.asm_instruction_t, dict[str, instr.explanation_t]]:
    assembly = instr.asm_instruction_t()
    explanations = {}
    tokens = _assembly_tokens(rules, record.encoded_in, record.assembly, set())
    for index, (text, displayed, rule_id) in enumerate(tokens):
        # The period in B.<cond> is part of the mnemonic that ADIS emits
        # before replaying its operand pattern.  Do not emit it a second time
        # as punctuation.  Other periods, such as vector element suffixes,
        # remain ordinary operands.
        if (
            text == "."
            and index > 0
            and index + 1 < len(tokens)
            and tokens[index - 1][0] in {"B", "BC"}
            and tokens[index + 1][1] == "<cond>"
        ):
            continue
        if displayed is None:
            operand = instr.asm_operand_t(text)
        else:
            link = f"operand_{index}"
            operand = instr.asm_operand_t(text, hover="", link=link)
            explanations[link] = instr.explanation_t(
                list(record.encoded_in[displayed]), [], _choice_table(record, rules, displayed, rule_id)
            )
        assembly.add(operand)
    assembly.bake("".join(text for text, _, _ in tokens))
    return assembly, explanations


def _choice_table(
    record: Encoding, rules: dict[str, JsonObject], displayed: str, rule_id: str | None
) -> instr.table_t | None:
    """Turn an encoded grammar choice into an ADIS operand lookup table."""

    if rule_id is None:
        return None

    fields = list(record.encoded_in[displayed])
    if displayed == "<cond>" and fields == ["cond"]:
        # The architecture grammar accepts condition-code aliases (CS/HS and
        # CC/LO), so its eighteen spellings are not a one-to-one bit table.
        # ADIS exposes the canonical sixteen encodings.
        return instr.create_cond_table()

    source_fields = {item.name: item for item in record.fields if item.name}
    if any(field not in source_fields for field in fields):
        return None

    if displayed == "<R>" and fields in (["option"], ["imm5"]):
        # ADD/SUB (extended register) and AdvSIMD scalar-register forms
        # select the source-register width from a compact grammar choice.
        # Both leave the complementary W-register range implicit, so neither
        # can be represented as an operand table.  ADIS's typed helpers cover
        # the complete architectural mappings.
        return None

    width = sum(source_fields[field].bit_range.width for field in fields)
    expected_choices = 1 << width

    def choice_pattern(choice: JsonObject) -> str | None:
        """Recover a choice's encoded bits from its named grammar rule.

        AARCHMRS uses a compact two-way grammar for some operands.  For
        example, ``R_00x_W`` and ``R_x11_X`` describe the register width of
        ADD (extended register).  The alternatives are ranges of raw field
        values, not choice indices.  The rule identifiers are the only
        machine-readable place where those ranges are represented.
        """

        patterns = []
        for symbol in choice.get("symbols", []):
            if symbol.get("_type") != "Instruction.Symbols.RuleReference":
                continue
            parts = [part for part in symbol["rule_id"].split("_") if part and not (set(part) - {"0", "1", "x"})]
            pattern = "".join(parts)
            if len(pattern) == width:
                patterns.append(pattern)
        return patterns[0] if len(patterns) == 1 else None

    def is_complete_choice(candidate: JsonObject) -> bool:
        return candidate["_type"] == "Instruction.Rules.Choice" and len(candidate["choices"]) == expected_choices

    rule = rules[rule_id]
    if not is_complete_choice(rule):
        source_patterns = [choice_pattern(choice) for choice in rule.get("choices", [])]
        if not all(pattern is not None for pattern in source_patterns):
            # The immediate grammar rule tells us that this operand is an
            # integer, while a choice rule may cover only a subset of its raw
            # field. In the latter case, use a complete same-display choice
            # when the JSON grammar provides one. For example, a particular
            # SVE instruction may spell only .S and .D even though the
            # standard <T> mapping spans all four size values.
            complete_choices = [
                candidate
                for candidate in rules.values()
                if candidate.get("display") == displayed and is_complete_choice(candidate)
            ]
            if complete_choices:
                rule = complete_choices[0]
            elif rule["_type"] != "Instruction.Rules.Choice":
                return None

        if rule["_type"] != "Instruction.Rules.Choice":
            return None

        if width == 5 and displayed.startswith(("<W", "<X")):
            choice_texts = {_choice_text(rules, choice) for choice in rule["choices"]}
            if (
                len(rule["choices"]) == 2
                and choice_texts & {"W", "X"}
                and choice_texts
                & {
                    "SP",
                    "WSP",
                    "WZR",
                    "XZR",
                }
            ):
                # AARCHMRS uses a two-branch grammar for general-purpose
                # registers: register 31 and an arbitrary register number.
                # Its choice index is not the five-bit encoded value, so it
                # cannot be represented by an operand table.  The established
                # typed helpers implement the register-31 rule directly.
                return None

        # Some operand spellings have only the encodings valid for this
        # instruction form.  Their listed order is the field-value order, so
        # preserve the partial table when no complete canonical spelling is
        # available.  This is needed for element-type forms such as <Ta>.

    patterns = [choice_pattern(choice) for choice in rule["choices"]]
    use_choice_patterns = all(pattern is not None for pattern in patterns)
    rows = []
    for index, choice in enumerate(rule["choices"]):
        text = _choice_text(rules, choice)
        if not text:
            # The empty form is represented by the absence of an operand only
            # after _expand_mnemonic_choices() has specialized it.
            return None
        bits = patterns[index] if use_choice_patterns else f"{index:0{width}b}"
        offset = 0
        row = []
        for field_name in fields:
            field_width = source_fields[field_name].bit_range.width
            row.append(bits[offset : offset + field_width])
            offset += field_width
        row.append(text)
        rows.append(row)
    header = [("bitfield", field) for field in fields] + [("symbol", displayed)]
    return instr.table_t(header, rows, displayed, idstr=record.operation_id)


def decorated_encodings(
    path: Path, implemented: set[str] | None = None, unimplemented_report: Path | None = None
) -> list[instr.decorated_encoding_t]:
    """Translate all concrete A64 encodings to ADIS's code-generation model."""

    rules = _load_document(path)["assembly_rules"]

    if implemented is None:
        implemented = _implemented_names(path)

    result = []
    candidates: list[tuple[Encoding, str]] = []
    for original in read_a64(path):
        records = [original, *_alias_records(original)]
        for record in records:
            for named_record in _fixed_system_alias_variants(record):
                for specialized, qualifier in _expand_system_aliases(named_record, rules):
                    for expanded in _expand_mnemonic_choices(specialized, rules):
                        candidates.append((expanded, qualifier))

    candidate_names = {record.name for record, _ in candidates}
    unknown = implemented - candidate_names
    if unknown:
        raise ValueError(f"Implementation policy contains unavailable entries: {sorted(unknown)}")
    if unimplemented_report is not None:
        with unimplemented_report.open("w") as output:
            json.dump(
                {
                    "aarchmrs_release": "2026-09_rel",
                    "implemented": sorted(implemented),
                    "unimplemented": sorted(candidate_names - implemented),
                },
                output,
                indent=2,
            )
            output.write("\n")

    for record, qualifier in candidates:
        if implemented is not None and record.name not in implemented:
            continue
        feature = _primary_feature(record.raw)
        semantic = _semantic_properties(record, feature)
        attributes = {
            "inst_id": record.operation_id,
            "arch_variant": feature,
            "feature": feature,
            "instr-class": _instruction_class(feature),
            "predicated": False,
            "takes_movprfx": False,
            "takes_pred_movprfx": False,
            "uses_dit": False,
            "may_read_flags": semantic["may_read_flags"],
            "may_write_flags": semantic["may_write_flags"],
            "semantic": semantic,
        }
        assembly, explanations = _make_assembly(record, rules)
        prototype = assembly.get_prototype()
        # The JSON grammar reserves ``Pg`` for an execution-governing
        # predicate.  Other predicate-register operands use names such as
        # Pn, Pm, and Pd and do not make an instruction predicated.
        attributes["predicated"] = "<Pg>" in prototype
        attributes["vector_srcs"] = _vector_source_count(record, feature, assembly)
        if feature != "FEAT_MOPS" and "]!" in prototype:
            semantic["pre_index"] = True
        source_record = _source_record(record.name, record.operation_id)
        decorated = instr.decorated_encoding_t(
            source_record,
            source_record,
            source_record,
            _make_encoding(record, ()),
            record.operation_id,
            record.name,
            attributes,
            assembly,
        )
        decorated.set_explanations(explanations)
        decorated.json_patterns = encoding_patterns(record)
        if qualifier != "INVALID":
            decorated.set_sys_alias_qualifier(qualifier)
        result.append(decorated)
    return result
