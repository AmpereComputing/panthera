#!/usr/bin/env python3
# Copyright (c) 2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# -*- python -*-
"""
Generate an enumerator header and cpp-file from a *enum file.
It also generates functions for converting to and from strings.
The format is simple yaml:

name: TYPE_NAME
namespace: NAMESPACE
cfile: CPP_FILE_NAME
hfile: HEADER_FILE_NAME
tokens:
- TOKEN1
- TOKEN2
...

To supply values, the tokens list elements can also be dictionaries:
   {'name': TOKEN3, 'value': 42}
The LAST_ITEM token is added at the end.
"""

import argparse
import datetime
import sys

import yaml


def _read_yaml(filename: str):
    with open(filename, "r") as fin:
        out = yaml.safe_load(fin)
    return out


def _copyright(fout):
    year = datetime.date.today().strftime("%Y")
    if year == "2023":
        oyear = year
    else:
        oyear = f"2023-{year}"
    print("//", file=fout)
    print(f"// Copyright (c) {oyear} Ampere Computing. All rights reserved.", file=fout)
    print("// SPDX-License-Identifier: BSD-3-Clause", file=fout)
    print("//", file=fout)


def _make_enum(fn: str):
    data = _read_yaml(fn)
    name = data["name"]
    cfile = data["cfile"]
    hfile = data["hfile"]
    namespace = data["namespace"]

    # supply sequential values if missing for the tokens
    tokens = []
    counter = 0
    for token in data["tokens"]:
        if isinstance(token, dict):
            assert "name" in token
            assert "value" in token
            tokens.append(token)
            counter = token["value"]
        else:
            tokens.append({"name": token, "value": counter})
        counter += 1

    # input validation
    assert name is not None
    assert cfile is not None
    assert hfile is not None
    assert namespace is not None
    assert len(tokens) > 0
    if "LAST_ITEM" in [token["name"] for token in tokens]:
        print(f"Cannot use LAST_ITEM as a token name")
        sys.exit(1)

    tokens.append({"name": "LAST_ITEM", "value": counter})

    print(f"\tWriting {hfile} ...")
    with open(hfile, "w") as hout:
        _copyright(hout)
        print(f"// generated file - do not edit", file=hout)
        print(f"#pragma once", file=hout)
        print(f"#include <optional>", file=hout)
        print(f"#include <string_view>", file=hout)
        print(f"#include <array>", file=hout)
        print(f"#include <utility>", file=hout)  # for std::to_underlying

        print(f"namespace {namespace} {{", file=hout)
        print(f"enum class {name} {{", file=hout)
        width = max([len(token["name"]) for token in tokens])
        for token in tokens:
            token_name = token["name"]
            value = token["value"]
            print(f"    {token_name:<{width}s} = {value},", file=hout)
        print(f"}};  //enum", file=hout)
        # this is for doing range-based for loops over the valid enumeration values
        print(f"constexpr std::array<{name}, std::to_underlying({name}::LAST_ITEM)> {name}_values = {{", file=hout)
        for token in tokens[:-1]:
            token_name = token["name"]
            print(f"    {name}::{token_name},", file=hout)
        print(f"}};  //array", file=hout)
        print(f"const std::string_view {name}2str({name} e);", file=hout)  # prototype
        print(f"std::optional<{name}> str2{name}(std::string_view s);", file=hout)  # prototype
        print(f"}} //namespace", file=hout)

    print(f"\tWriting {cfile} ...")
    with open(cfile, "w") as cout:
        _copyright(cout)
        print(f"// generated file - do not edit", file=cout)
        print(f"#include <string_view>", file=cout)
        print(f"#include <optional>", file=cout)
        print(f"#include <map>", file=cout)
        print(f'#include "{hfile}"', file=cout)
        print(f"namespace {namespace} {{", file=cout)
        print(f"const std::string_view {name}2str({name} e) {{", file=cout)
        print(f"    switch(e) {{", file=cout)

        prefix = f"{name}::"
        width = max([len(token["name"]) + len(prefix) for token in tokens])
        for token in tokens:
            token_name = token["name"]
            case_tag = f"{prefix}{token_name}"
            print(f'    case {case_tag:<{width}s}: return "{token_name}";', file=cout)
        print(f'    default: return "UNKNOWN";', file=cout)
        print(f"    }} // switch", file=cout)
        print(f"}} // function", file=cout)

        print(f"std::optional<{name}> str2{name}(std::string_view s) {{", file=cout)
        print(f"   static std::map<std::string_view, {name}> strmap = {{", file=cout)
        for token in tokens:
            token_name = token["name"]
            print(f'    {{ "{token_name}", {prefix}{token_name} }},', file=cout)
        print(f"   }}; // map", file=cout)
        print(f"   if (auto search = strmap.find(s); search != strmap.end()) {{", file=cout)
        print(f"       return search->second;", file=cout)
        print(f"   }} // if", file=cout)
        print(f"   return std::nullopt;", file=cout)
        print(f"}} // function", file=cout)

        print(f"}} //namespace", file=cout)


def _parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("input", nargs="+", help="Input file")
    args = parser.parse_args()
    return args


def _work():
    args = _parse_args()
    for fn in args.input:
        print(f"Processing {fn} ...")
        _make_enum(fn)


if __name__ == "__main__":
    _work()
