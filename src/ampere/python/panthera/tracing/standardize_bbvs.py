#!/usr/bin/env python3

#
# Copyright (c) 2018 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#

import argparse

from .bbv import BBV


def standardize_file(ifile, ofile):
    """
    Standardize a BBV file from virtual address-based dimensions/indexing to a
    dimensions consisting of the set of consecutive integers starting at 1 and
    extending through the number of dimensions represented. Reads BBVs from the
    ifile, standardizes their dimensions, and writes them out to ofile, which
    will be overwritten if it exists.
    """
    dimensions = {}
    ibbv = BBV()

    for line in ifile:
        if len(line) == 0:
            continue
        ibbv.from_bbv_string(line)
        obbv = BBV()
        for k, v in ibbv.items():
            if k not in dimensions:
                dimensions[k] = len(dimensions) + 1
            obbv[dimensions[k]] = v
        ofile.write(obbv.to_bbv_string(hex_format=False) + "\n")


def standardize(ifilename, ofilename):
    ifile = open(ifilename, "r")
    ofile = open(ofilename, "w")
    standardize_file(ifile, ofile)
    ifile.close()
    ofile.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Standardize BBV files for consumption by simpoint tool.")
    parser.add_argument("input_bbv", help="The BBV file to standardize", type=open)
    parser.add_argument("output_bbv", help="Where to write the standardized BBV file", type=argparse.FileType("w"))
    args = parser.parse_args()

    standardize_file(args.input_bbv, args.output_bbv)
    args.input_bbv.close()
    args.output_bbv.close()
