#!/usr/bin/python3
# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


import argparse
import json
import os
import sys
from glob import glob

parser = argparse.ArgumentParser()
parser.add_argument("--dryrun", action="store_true", default=False)
parser.add_argument("--filter", help="Only include entries including filenames contained in filter file", default=None)
parser.add_argument(
    "--no-verify", dest="verify", default=True, action="store_false", help="Do not verify output files afterwards"
)
parser.add_argument("--skip-missing", action="store_true", help="Skip files which are missing")
parser.add_argument("output_dir")
parser.add_argument("input", type=str, nargs="+", help="Input manifest files")

args = parser.parse_args()

if args.output_dir.startswith("/"):
    consolidation_dir = args.output_dir
elif len(args.input) > 1:
    consolidation_dir = os.path.join(os.path.commonpath(args.input), args.output_dir)
else:
    consolidation_dir = os.path.join(os.path.dirname(args.input), args.output_dir)

target_workloads = None
if args.filter is not None:
    target_workloads = set()
    with open(args.filter, "r") as filter_file:
        for target_workload_filename in filter_file:
            manifest_file_name = os.path.basename(target_workload_filename.strip())
            target_workloads.add(manifest_file_name)

workloads = {}
for input_manifest_filename in args.input:
    input_manifest_filename = os.path.realpath(input_manifest_filename)
    input_dir = os.path.dirname(input_manifest_filename)

    with open(input_manifest_filename) as input_manifest_file:
        input_manifest = json.load(input_manifest_file)
        for workload in input_manifest:
            if args.filter:
                skip_workload = True
                for element_name in workload:
                    if element_name.endswith("file"):
                        skip_workload = skip_workload and (
                            os.path.basename(workload[element_name]) not in target_workloads
                        )
                if skip_workload:
                    continue

            name = workload["name"]
            if name not in workloads:
                workloads[name] = []
            workloads[name].append((input_dir, workload))

output_json = []
for name, group in workloads.items():
    if len(group) > 1:
        print(f"Trace '{name}' duplicated, taking the last one...", file=sys.stderr)

    for input_dir, workload in reversed(group):
        workload_group = workload["workload"]
        skipped = set()
        for element_name in workload:
            if element_name.endswith("file"):
                workload_output_dir = os.path.join(consolidation_dir, workload_group)
                workload_output_path = os.path.join(workload_output_dir, os.path.basename(workload[element_name]))

                workload_input_path = os.path.join(input_dir, workload[element_name])
                if args.skip_missing and not os.path.exists(os.path.realpath(workload_input_path)):
                    print(f"{workload_input_path} does not exist, skipping...", file=sys.stderr)
                    skipped.add(element_name)
                    skipped.add(f"{element_name}_sha256sum")
                    continue

                if not os.path.exists(workload_output_dir) and not args.dryrun:
                    print(f"Making directory: {workload_output_dir}")
                    os.makedirs(workload_output_dir)

                if not args.dryrun:
                    os.symlink(os.path.relpath(workload_input_path, workload_output_dir), workload_output_path)
                workload[element_name] = os.path.relpath(f"{workload_output_path}", consolidation_dir)

        if "file" not in skipped:
            output_json.append({k: v for k, v in workload.items() if k not in skipped})
            break

if args.dryrun:
    print(
        f"Would have linked workloads into single path {consolidation_dir} and created {os.path.join(consolidation_dir, 'manifest.json')}:"
    )
    print(json.dumps(output_json, indent=4))

elif output_json:
    if not os.path.exists(consolidation_dir):
        os.makedirs(consolidation_dir)
    with open(os.path.join(consolidation_dir, "manifest.json"), "w") as manifest_file:
        json.dump(output_json, manifest_file, indent=4)
        print(
            f"Linked workloads into single path {consolidation_dir} and created {os.path.join(consolidation_dir, 'manifest.json')}"
        )

    if args.verify:
        missing = 0
        for file in glob(os.path.join(consolidation_dir, "**", "*")):
            if os.path.islink(file) and not os.path.exists(os.path.realpath(file)):
                print(f"Symlink {file} destination does not exist", file=sys.stderr)
                missing += 1

        if missing > 0:
            print(f"{missing} missing files", file=sys.stderr)
            sys.exit(1)
