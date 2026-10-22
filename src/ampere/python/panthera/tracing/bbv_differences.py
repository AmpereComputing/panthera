#!/usr/bin/env python3

#
# Copyright (c) 2018 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#

import argparse

from .bbv import BBV


def main():
    parser = argparse.ArgumentParser(
        description="Display the dimensions with the largest differences between the first BBV in each file specified"
    )
    parser.add_argument("--max-dimensions", help="The maximum number of dimensions to display", type=int, default=20)
    parser.add_argument("bbv1", help="The first BBV file", type=open)
    parser.add_argument("bbv2", help="The second BBV file", type=open)
    args = parser.parse_args()

    bbv1 = BBV()
    bbv1.from_bbv_string(args.bbv1.readline())
    args.bbv1.close()
    bbv2 = BBV()
    bbv2.from_bbv_string(args.bbv2.readline())
    args.bbv2.close()

    top_differences = sorted(
        list(set().union(bbv1.keys(), bbv2.keys())), key=lambda bb: abs(bbv2[bb] - bbv1[bb]), reverse=True
    )

    for bb in top_differences[: min(len(top_differences), args.max_dimensions)]:
        print("{:=#18x} {:= #12,d}".format(bb, bbv2[bb] - bbv1[bb]))


if __name__ == "__main__":
    main()
