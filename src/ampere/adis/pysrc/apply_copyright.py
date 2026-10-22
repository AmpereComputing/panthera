#!/usr/bin/env python3
#
# Copyright (c) 2022 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#

import argparse
import os

def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("copyrightheader",
                        nargs=1,
                        help="Copyright header")
    parser.add_argument("files",
                        default=[],
                        nargs="+",
                        help="Files to process")
    args = parser.parse_args()
    args.copyrightheader = args.copyrightheader[0]
    return args

def work():
    args = parse_args()
    with open(args.copyrightheader,'r') as fin:
        copyright_header = fin.readlines()

    for file_name in args.files:
        with open(file_name,'r') as fin:
            body = fin.readlines()
            new_file_name = f'{file_name}.new'
            with open(new_file_name,'w') as fout:
                fout.writelines(copyright_header)
                fout.writelines(body)
            os.replace(new_file_name, file_name)

if __name__ == "__main__":
    work()
