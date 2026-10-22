#!/usr/bin/env python3
# Copyright (c) 2019 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
This script is designed to be used with bbv_matrix.py. You will create a
matrix of vector distances using bbv_matrix.py, and then feed that .csv file
to this script to generate the plot.

Example:
$ ./bbv_matrix.py --combine-bbvs 100 /path/to/workload.bbv > workload_bbv_matrix.csv
$ ./plot_similarity_matrix.py workload_bbv_matrix.csv --workload workload \
    --image-filename similarity.png --instructions-per-interval "1 billion"
"""

# Ensure we don't try to write this to a display
import matplotlib

matplotlib.use("Agg")

import argparse

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.colors import PowerNorm


def make_similarity_chart(csv_file, workload_name, image_filename, instructions_per_interval=None):
    matrix = np.genfromtxt(csv_file, delimiter=",")

    plt.imshow(matrix, cmap="viridis_r", norm=PowerNorm(gamma=0.5))
    plt.title(f"{workload_name} BBV Similarity")
    colorbar = plt.colorbar()
    colorbar.set_label("basic block vector distance")
    if instructions_per_interval:
        plt.xlabel(f"instructions of execution ({instructions_per_interval} each)")
        plt.ylabel(f"instructions of execution ({instructions_per_interval} each)")
    else:
        plt.xlabel("instructions of execution")
        plt.ylabel("instructions of execution")
    plt.savefig(image_filename, bbox_inches="tight", dpi=600)


def main():
    parser = argparse.ArgumentParser(description="Create a BBV similarity chart given a CSV distance matrix")
    parser.add_argument("--workload", help="Name of the workload", type=str, default="Workload")
    parser.add_argument(
        "--image-filename",
        help="Filename at which to save the resulting image",
        type=str,
        default="similarity_matrix.png",
    )
    parser.add_argument(
        "--instructions-per-interval",
        help="String of the number of instructions per interval for axis labeling",
        type=str,
        default=None,
    )
    parser.add_argument("csv_matrix", help="The CSV BBV matrix file", type=open)
    args = parser.parse_args()

    make_similarity_chart(
        args.csv_matrix.name,
        args.workload,
        args.image_filename,
        instructions_per_interval=args.instructions_per_interval,
    )


if __name__ == "__main__":
    main()
