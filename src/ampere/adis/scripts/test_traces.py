#!/usr/bin/env python3
#
# Copyright (c) 2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# -*- python -*-
'''create is_disasm command lines for all the traces in the study
list.  The output is intended to be piped through a shell or 'run.par
-s -n 30 -o output.json' Any failed jobs will have nonzero exit status
and require further investigation.'''

import os
import errno
import collections
import argparse
import json
import re


class trace_info_t:
    def __init__(self, trace: str, suite: str, workload: str, weight: float, file_path: str):
        self.trace = trace
        self.suite = suite
        self.workload = workload
        self.weight = weight
        self.file = file_path

def read_trace_list(trace_list_fn):
    with open(trace_list_fn, 'r') as f:
        d = json.load(f)
    return d

def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('-tl', '--trace-list',
                        required=True,
                        help="Trace-list JSON file")
    parser.add_argument('-p', '--panthera-install',
                        default='install',
                        help="Panthera directory")
    args = parser.parse_args()
    return args

def work():
    args = parse_args()
    wklds = read_trace_list(args.trace_list)
    trace_info: dict[str: trace_info_t] = {x['name']: trace_info_t(x['name'], x['suite'],
                                                                   x['workload'], float(x['weight']),
                                                                   x['file']) for x in wklds}
    trace_dir = os.path.dirname(args.trace_list)
    files = [os.path.join(trace_dir, ti.file) for ti in trace_info.values()]
    def output(file: str) -> str:
        return '/dev/null'
        return re.sub(r'[.]is[.]xz', '.disasm', os.path.basename(file))
    cmds = [f'{args.panthera_install}/bin/is_disasm -a {file} > {output(file)}' for file in files]
    for cmd in cmds:
        print(cmd)

if __name__ == "__main__":
    work()

