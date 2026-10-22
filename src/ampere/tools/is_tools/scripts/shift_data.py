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
        # Doesn't write moves since too many to fit in one csv
        if "mov" in line:
            words = line.split()
        # Includes all normal instructions with shifts after
        elif ", lsl" in line or ", lsr" in line or ", asr" in line:
            words = line.split()
            words[-1] = words[-1].replace("]", "")
            if "lsl" in line:
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
            elif "lsr" in line:
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
                        "lsr",
                        words[-1][1:],
                    ]
                )
            elif "asr" in line:
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
                        "asr",
                        words[-1][1:],
                    ]
                )
        # Includes all shift instructions that use an immediate
        elif "lsl" in line or "lsr" in line or "asr" in line:
            words = line.split()
            if "x" not in words[-1]:
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
                        words[3],
                        words[-1][1:],
                    ]
                )
        # Includes all normal instructions with sign extends after
        elif ", sxtb" in line or ", sxth" in line or ", sxtw" in line:
            words = line.split()
            words[-1] = words[-1].replace("]", "")
            if "#" in line:
                write.writerow(
                    [
                        filename_split[dirs - 1],
                        filename_split[-2],
                        words[0],
                        words[1][:-1],
                        words[2],
                        words[3],
                        "True",
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
                        "True",
                        "False",
                        "N/A",
                        0,
                    ]
                )
        # Includes all normal instructions with zero extends after
        elif ", uxtb" in line or ", uxth" in line or ", uxtw" in line:
            words = line.split()
            words[-1] = words[-1].replace("]", "")
            if "#" in line:
                write.writerow(
                    [
                        filename_split[dirs - 1],
                        filename_split[-2],
                        words[0],
                        words[1][:-1],
                        words[2],
                        words[3],
                        "False",
                        "True",
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
                        "True",
                        "N/A",
                        0,
                    ]
                )
        # Includes all zero extend instructions
        elif "uxtb" in line or "uxth" in line or "uxtw" in line:
            words = line.split()
            write.writerow(
                [
                    filename_split[dirs - 1],
                    filename_split[-2],
                    words[0],
                    words[1][:-1],
                    words[2],
                    words[3],
                    "False",
                    "True",
                    "N/A",
                    0,
                ]
            )
        # Includes all sign extend instructions
        elif "sxtb" in line or "sxth" in line or "sxtw" in line:
            words = line.split()
            write.writerow(
                [
                    filename_split[dirs - 1],
                    filename_split[-2],
                    words[0],
                    words[1][:-1],
                    words[2],
                    words[3],
                    "True",
                    "False",
                    "N/A",
                    0,
                ]
            )
        # Includes all REV / RBIT instructions
        elif "rev" in line or "rbit" in line:
            words = line.split()
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
                    "N/A",
                    0,
                ]
            )
        # Records instruction count for total
        elif "instruction_count" in line:
            words = line.split()
            instructions = int(words[2])
    return instructions


dirs = len(sys.argv[1].split("/"))
instr = 0
with open("simple_shifter.csv", "wb") as csvfile:
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
