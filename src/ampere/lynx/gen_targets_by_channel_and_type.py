#!/usr/bin/env python3

# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Read Excel tables of allowed communication to generate target rules.

The spreadsheet is organized into tabs per CHI channel (REQ, RSP, SNP, DAT). Each table
has a source entity in the rows and a target entity in the columns (with headers). The
contents of the table are:

    X - These entities do not communication on this channel.
    L - These entities communicate, but only on the local die.
    R - These entities communicate across local AND remote die (there's no "remote only")
"""

import argparse
import sys

import pandas as pd


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", help="input (Excel) file")
    parser.add_argument("-o", "--output", help="output file name (normally a .cc file name)")
    return parser.parse_args()


def process_tab(args, channel, outfile):
    df = pd.read_excel(args.input, sheet_name=channel)
    for row in range(len(df)):
        source = df.iloc[row][channel]
        targets = {}
        for col in df.columns:
            if col == channel:
                continue
            entry = df.iloc[row][col]
            if entry != "X":
                targets[col] = entry
        target_list = ", ".join([f"{{{k}, {v}}}" for k, v in targets.items()])
        print(f"            {{ {source}, {{{target_list}}} }},", file=outfile)


def process(args, outfile):
    print(
        rf"""
// This is generated file. DO NOT EDIT!
//
// Generation command:
//
//    python3 gen_targets_by_channel_and_type.py {args.input} -o {args.output}
//
#include <map>
#include <vector>
          
#include <ampere/lynx/rules.h>

#define L Scope::SameDie
#define R Scope::CrossDie

namespace ampere::lynx {{

using enum ComponentType;

const std::map<Channel, std::map<ComponentType, std::vector<std::pair<ComponentType, Scope>>>>
Rules::m_default_targets_by_channel_and_type = {{
""",
        end="",
        file=outfile,
    )
    for chan in ("REQ", "RSP0", "RSP1", "SNP", "DAT"):
        print("    {", file=outfile)
        print(f"        Channel::{chan},", file=outfile)
        print("        {", file=outfile)
        process_tab(args, "RSP" if chan in {"RSP0", "RSP1"} else chan, outfile)
        print("        }", file=outfile)
        print("    },", file=outfile)
    print(
        r"""};

} // namespace ampere::lynx

#undef L
#undef R
""",
        end="",
        file=outfile,
    )


def generate(args):
    if args.output:
        with open(args.output, "w") as outfile:
            process(args, outfile)
    else:
        process(args, sys.stdout)


if __name__ == "__main__":
    generate(parse_args())
