#!/usr/bin/env python3
# Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Produce C++ definitions (primarily enums) and functions from tabular specs."""

import argparse
import os
import sys

from . import TabFile, Table, c_out, py_out


def main():
    args = parse_args()
    tabfile = TabFile(args.tabfile)

    if args.python:
        py_out.print_py_output(tabfile.tables, tabfile.global_vars, output_file(args.python))

    if args.header:
        c_out.print_cc_header(tabfile.tables, tabfile.global_vars, output_file(args.header))

    if args.ccfile:
        c_out.print_cc_file(
            tabfile.tables, tabfile.global_vars, output_file(args.ccfile), os.path.basename(args.header)
        )


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tabfile", help="Table file containing table definition(s)")
    parser.add_argument("-H", "--header", help="C++ header out")
    parser.add_argument("-c", "--ccfile", help="C++ source out")
    parser.add_argument("-p", "--python", help="Python output")
    return parser.parse_args()


def output_file(name):
    dir = os.path.dirname(name)
    if dir and not os.path.exists(dir):
        os.mkdir(dir)
    return open(name, "w")


if __name__ == "__main__":
    main()
