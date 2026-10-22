# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import argparse


def to_bbv_string(vector, hex_format=True):
    """
    Export vector as a string in the same format from_bbv_string imports
    """
    if hex_format:
        return "P" + " ".join([":0x%x:%d" % (k, vector[k]) for k in vector.keys()])
    else:
        return "T" + " ".join([":%d:%d" % (k, vector[k]) for k in vector.keys()])


# Borrowed (with minor modifications) from bbv.py
def from_mav_string(s):
    """
    Import a vector from a string taken from a simpoint-style BBV file.
    These are in the format:
        T:1:49 :2:494 :3:94 :4:182
    or
        P:0x5dd8dc:2 :0x50b284:938 :0x50b170:38

    Where the first character is the type prefix, and dimensions are
    space-delimited. The first number within each colon-delimited grouping
    is the dimension of the vector, and the second number is the magnitude
    (essentially 'frequency x length' of the basic block).

    Lines prefixed with 'P' are assumed to include full hexadecimal
    addresses as dimensions (i.e.  from the Plugin), while lines prefixed
    with 'T' are assumed to have decimal dimensions.
    """
    vector = {}
    if s.startswith("T"):
        base = 10
    elif s.startswith("P"):
        base = 16
    else:
        raise Exception("Invalid BBV format")

    for dim in s[1:].split():
        dims = dim.split(":", maxsplit=2)
        vector[int(dims[1], base)] = int(dims[2].split(",")[0]) + int(dims[2].split(",")[1])

    return vector


# Borrowed (with minor modifications) from bbv.py
def from_mav_string_local(s):
    vector = {}
    if s.startswith("T"):
        base = 10
    elif s.startswith("P"):
        base = 16
    else:
        raise Exception("Invalid MAV format")

    for dim in s[1:].split():
        dims = dim.split(":", maxsplit=2)
        vector[int(dims[1], base)] = int(dims[2].split(",")[0]) + int(dims[2].split(",")[1])
    return vector


def sum_file(mavfile):
    # Read the input file and calculate the sum for each key
    total_vector = {}
    for line in mavfile:
        line_vector = from_mav_string(line)
        for key, value in line_vector.items():
            if key in total_vector:
                total_vector[key] += value
            else:
                total_vector[key] = value

    print("\n".join("0x%x,%d" % (k, total_vector[k]) for k in total_vector.keys()))


def similarity_file(mavfile):
    # Read the input file and calculate the sum for each key
    for line in mavfile:
        vector = from_mav_string_local(line)
        print(to_bbv_string(vector) + "\n")


def main():
    parser = argparse.ArgumentParser(description="")
    parser.add_argument("mav", help="The MAV file", type=open)
    parser.add_argument("--self-similarity", help="Create MAV file for self similarity", action="store_true")
    parser.add_argument("--sum-unique-address", help="Aggregate accesses to uniqe address", action="store_true")
    args = parser.parse_args()

    if args.self_similarity:
        similarity_file(args.mav)
    elif args.sum_unique_address:
        sum_file(args.mav)
    else:
        parser.print_usage()
        exit(0)


if __name__ == "__main__":
    main()
