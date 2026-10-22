# Copyright (c) 2019-2021 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import csv
import fnmatch
import gzip
import os
import sys


def process(filename):
    print(filename)
    movs = []
    # Read all movs into list
    f = gzip.open(filename, "rb")
    filename_split = filename.split("/")
    for line in f:
        # Create dict of info for each mov
        if "movz" in line or "movn" in line or "movk" in line:
            words = line.split()
            entry = {}
            entry["pc"] = int(words[0][2:-1], 16)
            entry["instr"] = words[3]
            entry["dest"] = words[4][:-1]
            entry["quantity"] = words[1][:-1]
            if "lsl" in line:
                entry["shift"] = words[7][1:]
            else:
                entry["shift"] = 0
            movs.append(entry)

    instruction_window = int(sys.argv[2])
    ignore = []
    i = 0
    # Loop through list of movs
    while i < len(movs):
        if movs[i] not in ignore:
            count = 0
            shifts = str(movs[i]["shift"])
            if len(sys.argv) > 3 and sys.argv[3] == "-t":
                shifts = shifts + movs[i]["instr"][-1]
            # Loop through next 15 movs
            while count < instruction_window and (i + count + 1) < len(movs):
                # If pc already out of window, break
                if abs(movs[i + count + 1]["pc"] - movs[i]["pc"]) > 4 * instruction_window:
                    break
                if (movs[i + count + 1]["dest"] == movs[i]["dest"]) and (
                    movs[i + count + 1]["quantity"] == movs[i]["quantity"]
                ):
                    # If movz or movn to same register, end pattern
                    if (movs[i + count + 1]["instr"] == "movz") or (movs[i + count + 1]["instr"] == "movn"):
                        break
                    # If movk to same register and within instr window (using pc), append to pattern
                    if (
                        (movs[i + count + 1]["instr"] == "movk")
                        and (abs(movs[i + count + 1]["pc"] - movs[i]["pc"]) <= 4 * instruction_window)
                        and (movs[i + count + 1] not in ignore)
                    ):
                        ignore.append(movs[i + count + 1])
                        shifts = shifts + " " + str(movs[i + count + 1]["shift"])
                count += 1
            write.writerow([filename_split[dirs - 1], filename_split[-1], movs[i]["quantity"], shifts])
        i += 1


dirs = len(sys.argv[1].split("/"))
with open("mov_all_patterns.csv", "wb") as csvfile:
    write = csv.writer(csvfile, delimiter=",")
    write.writerow(["Benchmark", "Test Case", "Quantity", "Pattern"])
    for root, dirnames, filenames in os.walk(sys.argv[1]):
        for filename in fnmatch.filter(filenames, "*.is.dis.gz"):
            process(os.path.join(root, filename))
