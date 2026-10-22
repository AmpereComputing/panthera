#!/usr/bin/env python3
# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


import argparse
import os
import sys
from pathlib import Path

from panthera.cheetah import Job, RollupStudy

from ..utils import Cluster
from . import setup_logging


def parse_args():
    parser = argparse.ArgumentParser(description="Retrieve/Send missing data/collateral to interactive cluster")
    parser.add_argument(
        "--debug",
        action="store_true",
        help="Enable debug logging",
    )
    parser.add_argument("--file", action="append", default=[], type=argparse.FileType("r"), help="copy outputs file")
    parser.add_argument(
        "--job", action="append", default=[], type=argparse.FileType("r"), help="copy outputs listed in job JSON file"
    )
    parser.add_argument("--study", type=argparse.FileType("r"), help="copy outputs listed in study JSON file")
    parser.add_argument("--push", action="store_true", help="push files to all other clusters")
    parser.add_argument("--ssh-identity-file", default="~/.ssh/id_slurm", type=str, help="SSH key to use")
    return parser.parse_args()


def find_study_directory(jobfile):
    jobfile_directory = Path(jobfile).absolute().parent
    study_directory_parts = jobfile_directory.parts
    for i in range(len(study_directory_parts) + 1):
        study_file = Path(*study_directory_parts[:i], "study.json")
        if study_file.exists():
            study_directory = study_file.parent
            break

    if study_directory is None:
        raise RuntimeError("Cannot find study directory")
    return Path(study_directory).absolute()


if __name__ == "__main__":
    args = parse_args()
    setup_logging(debug=args.debug)

    # Set of files to find
    files = set()

    for f in args.file:
        files.add(Path(f.name).absolute())

    # Get outputs from a job.json file
    for j in args.job:
        study_directory = find_study_directory(j.name)
        job = Job.from_json_file(j.name)
        for meta in job.cmds_meta:
            outputs = meta.get("outputs", {})
            files.update({os.path.join(study_directory, output) for output in outputs})

    # Get outputs from a study.json
    if args.study:
        study_json_path = args.study.name
        study_directory = str(Path(study_json_path).absolute().parent)
        study = RollupStudy.from_json(args.study, study_directory=study_directory)
        for job in study.jobs:
            for meta in job.cmds_meta:
                outputs = meta.get("outputs", {})
                files.update({os.path.join(study_directory, output) for output in outputs})

    # If we have no files to copy or we are already on the login cluster we are done
    if len(files) == 0:
        sys.exit(0)

    # Push file contents to other clusters
    if args.push:
        login_cluster = Cluster.login_cluster()
        if Cluster.my_cluster() == login_cluster:
            sys.exit(0)
        with Cluster(login_cluster, os.path.expanduser(args.ssh_identity_file)) as c:
            c.put(files)
    else:
        raise NotImplementedError("Retrieving files not supported")
