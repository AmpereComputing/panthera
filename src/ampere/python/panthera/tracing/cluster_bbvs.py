#!/usr/bin/env python3

#
# Copyright (c) 2019 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#

# This script can be used to calculate which intervals may be chosen (and at
# which weightings) to represent a larger set of instruction streams.
#
# Example usage:
#
# First, calculate the BBVs based off of the collected instruction streams:
#    $ for file in $(ls /path/to/streams*.is.xz); do echo ./install/bin/is2bbv $file $(basename $file .is.xz).bbv; done > parallel_commands
#    $ parallel -j 8 < parallel_commands
#
# Next, concatenate all of the BBVs together into one file (assuming there are
# 77 streams collected and the BBV files are named in the form streams_5.bbv):
#    $ for i in $(seq 0 76); do cat streams_${i}.bbv >> all_streams.bbv ; done
#
# Finally, run this script on those collected BBVs:
#    $ ./cluster_bbvs.py all_streams.bbv
#
# It will print out a listing of the selected streams, zero-indexed by the
# order they were in the BBV file provided as input to this script. It also
# calculates the Euclidean and Manhattan distances between the sum of all the
# input BBVs and the weighted sum of the BBVs corresponding to the selected
# streams. Because the vectors are first normalized, the maximum (worst)
# values for these distances at sqrt(2)==1.414 and 2, respectively. These
# distances can be used to estimate how good of a fit the selected intervals
# are. Distances under 0.01 or 0.02 are generally quite good, but higher values
# can be fine too depending on how representative you want it to be.

import argparse

import numpy as np
import pandas as pd
from scipy.cluster.vq import kmeans, vq, whiten

from .bbv import BBV


def standardize_file(ifile):
    dimensions = {}
    ibbv = BBV()
    vectors = []

    for line in ifile:
        if len(line) == 0:
            continue
        ibbv.from_bbv_string(line)
        obbv = BBV()
        for k, v in ibbv.items():
            if k not in dimensions:
                dimensions[k] = len(dimensions) + 1
            obbv[dimensions[k]] = v
        vectors.append(obbv)

    return vectors


def cluster(ifile):
    min_k = 2
    max_k = 20
    pct_best = 1.15  # Accept any within 15% of best
    kmeans_iter = 20  # Number of random seeds to try for each clustering count

    print("Calculating standardized BBVs...")
    obbv = standardize_file(ifile)

    if max_k > len(obbv):
        max_k = len(obbv)

    print("Creating pandas dataframe and normalizing data...")
    df = pd.DataFrame(obbv)
    df = df.fillna(value=0.0)
    whitened = whiten(df)

    results = {}
    best = None
    best_clustering = None
    for k in range(min_k, max_k + 1):
        print("Creating clustering for", k, "clusters...")
        codebook, distortion = kmeans(whitened, k, iter=kmeans_iter)
        results[k] = (codebook, distortion)
        if best is None or distortion < best:
            best = distortion
            best_clustering = k
        print("Clustered with distortion", distortion)

    # Choose which clustering provides the lowest distortion with the fewest
    # clusters
    good_enough = None
    for k in range(min_k, max_k + 1):
        distortion = results[k][1]
        if distortion <= pct_best * best:
            if good_enough is None or k < good_enough:
                good_enough = k
    print("Choosing clustering for", good_enough, "clusters with distortion", results[good_enough][1])

    # Create a list of BBV objects from the centroids of the clusters
    centroids = []
    for c in results[good_enough][0]:
        centroid = BBV()
        index = 1  # yes, this really is one-indexed
        for v in c:
            centroid[index] = v
            index += 1
        centroids.append(centroid)

    # Create a list of BBV objects from the centroids of the clusters
    whitened_bbvs = []
    for c in whitened:
        vector = BBV()
        index = 1  # yes, this really is one-indexed
        for v in c:
            vector[index] = v
            index += 1
        whitened_bbvs.append(vector)

    # Map all intervals onto chosen centroids
    print("Mapping intervals onto chosen clustering...")
    clusters = {}
    for i, vector in enumerate(whitened_bbvs):
        closest = None
        distance = None
        for c, centroid in enumerate(centroids):
            d = vector.euclidean_distance(centroid)
            if closest is None or d < distance:
                closest = c
                distance = d
        if closest not in clusters:
            clusters[closest] = [i]
        else:
            clusters[closest].append(i)

    # Find the vector closest to the centroid for this cluster
    print("Choosing intervals to represent clusters...")
    cluster_vectors = {}
    for cluster, vectors in clusters.items():
        closest = None
        distance = None
        for v in vectors:
            vector = whitened_bbvs[v]
            d = vector.euclidean_distance(centroids[cluster])
            if closest is None or d < distance:
                closest = v
                distance = d
        cluster_vectors[cluster] = closest

    print("\nSelected streams (zero-indexed by line number in input BBV file) and their weights:")
    total_vectors = len(obbv)
    weights = {}
    for cluster, vector in cluster_vectors.items():
        weight = 1.0 * len(clusters[cluster]) / total_vectors
        weights[cluster] = weight
        print(vector, weight)

    print("\nCalculating distance between sum of all BBVs and the weighted sum of the selected intervals...")
    total_bbvs = BBV()
    weighted_bbvs = BBV()
    for b in obbv:
        total_bbvs += b
    for cluster, vector in cluster_vectors.items():
        weighted_bbvs += obbv[vector] * (weights[cluster] * total_vectors)
    print("Euclidean distance:", total_bbvs.euclidean_distance(weighted_bbvs))
    print("Manhattan distance:", total_bbvs.manhattan_distance(weighted_bbvs))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Find clusterings for file of BBVs")
    parser.add_argument("input_bbv", help="The BBV file to cluster", type=open)
    args = parser.parse_args()

    cluster(args.input_bbv)
