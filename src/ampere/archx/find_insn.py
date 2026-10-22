#!/usr/bin/env python3

# Copyright (c) 2018 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import argparse
import re

import uasm
from pattern import Pattern


class Finder(uasm.InsnHandler):
    def __init__(self, pattern):
        self.pattern = Pattern(32)
        self.pattern.add_pattern(self.canonize(pattern))

    def handle_instruction(self, insn):
        for p in insn.patterns:
            try_pattern = Pattern(32)
            try_pattern.add_pattern(p.replace(" ", ""))
            if self.pattern.p_intersects(try_pattern):
                print(insn.file, insn.line)
                break

    @staticmethod
    def canonize(pattern):
        if re.match("^0x[0-9A-Fa-f]+$", pattern):
            return "{:032b}".format(int(pattern, 16))
        else:
            return pattern.replace(" ", "")


def main():
    args = parse_args()
    finder = Finder(args.match)
    for uasm_file in args.input:
        try:
            uasm.parse(uasm_file, finder)
        except Exception as e:
            print("exception while parsing {}: {}".format(uasm_file, e))
            raise e


def parse_args(arguments=None):
    parser = argparse.ArgumentParser(description="Find uasm matching an instruction pattern")
    parser.add_argument("--match", "-m", help="Pattern to match", required=True)
    parser.add_argument("input", help="Input .uasm file", nargs="*")
    return parser.parse_args(arguments)


if __name__ == "__main__":
    main()
