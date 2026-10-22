# Copyright (c) 2019-2021 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import csv
import fnmatch
import gzip
import os
import sys


def process(filename):
    print(filename)
    instructions = 0
    f = gzip.open(filename, "rb")
    filename_split = filename.split("/")
    for line in f:
        words = line.split()
        if (
            (("mov" in line or "fmov" in line) and "#" in line)
            or "movz" in line
            or "movk" in line
            or "movn" in line
            or "movi" in line
        ):
            if "lsl" in line:
                words[-1] = words[-1].replace("]", "")
                write.writerow(
                    [
                        filename_split[dirs - 1],
                        filename_split[-2],
                        words[0],
                        words[1][:-1],
                        words[2],
                        words[3],
                        "False",
                        "False",
                        "lsl",
                        words[-1][1:],
                    ]
                )
            else:
                write.writerow(
                    [
                        filename_split[dirs - 1],
                        filename_split[-2],
                        words[0],
                        words[1][:-1],
                        words[2],
                        words[3],
                        "False",
                        "False",
                        "none",
                        0,
                    ]
                )
        elif "instruction_count" in line:
            words = line.split()
            instructions = int(words[2])
    return instructions


dirs = len(sys.argv[1].split("/"))
instr = 0
with open("simple_shifter_mov.csv", "wb") as csvfile:
    write = csv.writer(csvfile, delimiter=",")
    write.writerow(
        [
            "Benchmark",
            "Test Case",
            "PC",
            "Quantity",
            "Opcode",
            "Instruction",
            "Sign Extend",
            "Zero Extend",
            "Shift Direction",
            "Shift Amount",
        ]
    )
    for root, dirnames, filenames in os.walk(sys.argv[1]):
        for filename in fnmatch.filter(filenames, "*.is.dis.gz"):
            instr += process(os.path.join(root, filename))

print("Total Instructions: " + str(instr))
