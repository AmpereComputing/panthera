#!/usr/bin/env python3

#
# Copyright (c) 2018 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#

import argparse

from .bbv import BBV


def main():
    parser = argparse.ArgumentParser(
        description="Calculate the Euclidean (the default) or Manhattan distance between the first BBV in each of the supplied files."
    )
    parser.add_argument("--manhattan", help="Calculate a Manhattan distance instead of Euclidean", action="store_true")
    parser.add_argument("bbv1", help="The first BBV file", type=open)
    parser.add_argument("bbv2", help="The second BBV file", type=open)
    args = parser.parse_args()

    bbv1 = BBV()
    bbv1.from_bbv_string(args.bbv1.readline())
    args.bbv1.close()
    bbv2 = BBV()
    bbv2.from_bbv_string(args.bbv2.readline())
    args.bbv2.close()

    if args.manhattan:
        print("%f" % (bbv1.manhattan_distance(bbv2)))
    else:
        print("%f" % (bbv1.euclidean_distance(bbv2)))


if __name__ == "__main__":
    main()
