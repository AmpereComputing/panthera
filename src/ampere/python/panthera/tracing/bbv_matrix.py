#!/usr/bin/env python3

#
# Copyright (c) 2019 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#

# This script is designed to be used with plot_similarity_matrix.py. You will
# create a matrix of vector distances using this script, and then feed that
# .csv file to plot_similarity_matrix.py to generate the plot.
#
# Example:
# $ ./bbv_matrix.py --combine-bbvs 100 /path/to/workload.bbv > workload_bbv_matrix.csv
# $ ./plot_similarity_matrix.py workload_bbv_matrix.csv --workload workload \
#     --image-filename similarity.png --instructions-per-interval "1 billion"

import argparse
import concurrent.futures
import sys
import time

from .bbv import BBV


def kernel(i, bbvs, manhattan):
    row = []
    for j in range(len(bbvs)):
        if j < i:
            row.append(-1)
        else:
            if manhattan:
                row.append(bbvs[i].manhattan_distance(bbvs[j]))
            else:
                row.append(bbvs[i].euclidean_distance(bbvs[j]))
    return {i: row}


def make_matrix(bbvfile, combine_bbvs=1, manhattan=False, num_threads=5):
    bbvs = []

    num_aggregated = 0
    aggregate_bbv = BBV()
    for line in bbvfile:
        tmp_bbv = BBV()
        if len(line) == 0:
            continue
        tmp_bbv.from_bbv_string(line)
        aggregate_bbv += tmp_bbv
        num_aggregated += 1

        if num_aggregated == combine_bbvs:
            num_aggregated = 0
            bbvs.append(aggregate_bbv)
            aggregate_bbv = BBV()

    rows = {}
    start_time = time.time()
    with concurrent.futures.ProcessPoolExecutor(max_workers=num_threads) as executor:
        executors = []
        for i in range(len(bbvs)):
            executors.append(executor.submit(kernel, i, bbvs, manhattan))
        for executor_result in concurrent.futures.as_completed(executors):
            try:
                executor_dict = executor_result.result()
                rows.update(executor_dict)
            except Exception as exc:
                print(f"Generated an exception {exc}")
                exit(-1)

    values = []
    for i in range(len(bbvs)):
        values = rows[i]
        for j in range(0, i):
            assert values[j] == -1
            values[j] = rows[j][i]

        print(",".join([str(d) for d in values]))

    end_time = time.time()
    print(f"Execution time = {end_time - start_time}", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(
        description="Write CSV to stdout containing the Euclidean (the default) or Manhattan distance between all BBVs and all other BBVs in a BBV file."
    )
    parser.add_argument("--manhattan", help="Calculate a Manhattan distance instead of Euclidean", action="store_true")
    parser.add_argument(
        "--combine-bbvs",
        help="Combine this many BBV lines into one before calculating distance matrix",
        type=int,
        default=1,
    )
    parser.add_argument("bbv", help="The first BBV file", type=open)
    parser.add_argument("--num-threads", help="Number of threads creating BBV matrix", type=int, default=5)
    args = parser.parse_args()

    make_matrix(args.bbv, combine_bbvs=args.combine_bbvs, manhattan=args.manhattan, num_threads=args.num_threads)


if __name__ == "__main__":
    main()
