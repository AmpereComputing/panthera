#!/usr/bin/env python3
# Copyright (c) 2022-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


"""
Create a study list which is a subset of another study list.

The traces can be included/excluded by matching regular expressions on one of
their fields. Currently only matching one field per run is supported (i.e. all
inclusion or exclusion patterns can only be matched against one of
trace/workload/suite per run), but you could chain together multiple calls of
this script.

You can specify inclusion patterns (regular expressions) one per line in a file
via `--include-file`, or simply add them as positional arguments on the
command-line with `--include`. Exclusions can be similarly specified via the
command-line with `--exclude-file` or `--exclude`. If you want to include every
trace in the input file except those specifically excluded, pass `--include
.` in addition to your exclusion patterns.

You can also filter traces by a minimum weight using the `--min-weight`
parameter. Traces will only be included if their weight field is above this
threshold. Note that 5% would be specified by 0.05.

Any trace is included in the output trace list if all of the following are true:
1. That trace is included by at least one inclusion pattern
2. That trace is not excluded by any exclusion pattern
3. That trace's weight is above the minimum weight specified (defaults to 0)
"""

import argparse
import json
import logging
import re
import sys
from itertools import chain
from os.path import abspath, dirname, join, realpath

import pandas as pd

from panthera import setup_logging


def parse_args():
    parser = argparse.ArgumentParser(description="Filter the study list, producing a list that is a subset of it.")
    parser.add_argument(
        "--input-list",
        type=str,
        required=True,
        help="Study list to filter from",
    )
    parser.add_argument(
        "--output-list",
        type=str,
        required=True,
        help="Location to write filtered study list",
    )

    filtering = parser.add_argument_group("Filtering")
    filtering.add_argument(
        "--min-weight",
        type=float,
        default=0.0,
        help="Only include traces with at least this weight. Enter 0.05 for 5%% (default: 0.00)",
    )
    filtering.add_argument(
        "--workload-weights-sum",
        type=float,
        default=sys.float_info.max,
        help="Include the highest-weighted traces in each workload (after all other filtering) which add up to a total weight of at least the amount specified by this option. Re-weight included traces such that each workload adds up to 100%% again. Enter 0.05 for 5%% (default: max float)",
    )
    filtering.add_argument(
        "--match",
        type=str,
        default="workload",
        choices=["name", "trace", "workload", "suite"],
        help="The field in the study list to filter against (name==trace)",
    )
    filtering.add_argument(
        "--include",
        action="append",
        help="Patterns to match against (regular expressions). Any trace matching any filter is included.",
    )
    filtering.add_argument(
        "--include-file",
        help="File containing one pattern per line to match against (regular expressions). Any trace matching any filter is included.",
    )
    filtering.add_argument(
        "--exclude",
        action="append",
        help="Exclude any traces which match this pattern, even if they would otherwise be included",
    )
    filtering.add_argument(
        "--exclude-file",
        help="File containing one pattern per line to match against (regular expressions). Any trace matching any filter is excluded.",
    )
    filtering.add_argument("--rollup", help="Add metrics from trace-level rollup for filtering or sorting")

    modify = parser.add_argument_group("Modify")
    modify.add_argument(
        "--rename",
        nargs=3,
        metavar=("FIELD", "MATCH", "REPLACE"),
        action="append",
        help="Rename field values which match using replacement",
    )
    modify.add_argument(
        "--assign-by-index",
        nargs=4,
        metavar=("INDEX_FIELD", "INDEX", "VALUE_FIELD", "VALUE"),
        action="append",
        help="Assign value for field by index",
    )
    modify.add_argument(
        "--assign-by-index-file",
        help="Assign value for field by index from file (same format as --assign-by-index per line)",
    )
    modify.add_argument(
        "--normalize",
        action="store_true",
        help="Normalize all weights in workload to sum to 1.0 (after filtering)",
    )

    ordering = parser.add_argument_group("Ordering")
    ordering.add_argument(
        "--sort-descending",
        action="store_true",
        help="Sort traces descending (by value specified in --sort-by), otherwise sort ascending by default",
    )
    ordering.add_argument(
        "--sort-by",
        help="Sort by field value",
    )
    args = parser.parse_args()

    if args.match == "trace":
        args.match = "name"

    if args.min_weight > 0.0 and args.workload_weights_sum < sys.float_info.max:
        logging.warning(
            "Both --min-weight and --workload-weights-sum were specified on the command-line. The resulting study list may not be what you expect."
        )

    return args


def compile_filters(filters):
    """Compile regex filters."""
    return [re.compile(f) for f in filters] if filters else []


def read_filter_file(path):
    """Read and compile filters from file."""
    with open(path, "r") as filter_file:
        return compile_filters([line.strip() for line in filter_file])


def gather_workload_traces(traces):
    """Gather traces by workload."""
    workload_traces = {}
    for trace in traces:
        workload = trace["workload"]
        if workload not in workload_traces:
            workload_traces[workload] = []
        workload_traces[workload].append(trace)

    return workload_traces


def normalize_weights_within_workload(traces):
    """Normalize the sum of weights within a workload to 1."""
    workload_total_weight = {
        name: sum(float(t["weight"]) for t in workload_traces)
        for name, workload_traces in gather_workload_traces(traces).items()
    }
    for trace in traces:
        trace["weight"] = float(trace["weight"]) / workload_total_weight[trace["workload"]]


def rename_field_values(renames, traces):
    """Rename values of fields which match."""
    for field, match, sub in renames:
        matcher = re.compile(match)
        for trace in traces:
            trace[field] = matcher.sub(sub, trace[field])


def assign_field_values(assigns, traces):
    """Rename values of fields which match."""
    for index_field, index, value_field, value in assigns:
        for trace in traces:
            if trace[index_field] == index:
                trace[value_field] = value


def main(args):
    includes = compile_filters(args.include)
    if args.include_file:
        includes.extend(read_filter_file(args.include_file))

    excludes = compile_filters(args.exclude)
    if args.exclude_file:
        excludes.extend(read_filter_file(args.exclude_file))

    with open(args.input_list, "r") as study_list_file:
        study_list = json.load(study_list_file)

    required_fields = frozenset(["name", "file", "workload", "suite", "weight"])
    original_fields = set(chain.from_iterable([trace.keys() for trace in study_list]))
    keep_fields = required_fields | original_fields

    rollup = None
    if args.rollup:
        rollup = pd.read_csv(args.rollup).set_index("trace").drop_duplicates().select_dtypes(include="number")
        rollup = rollup.loc[:, list(set(rollup.columns) - keep_fields)]

    original_number_of_traces = len(study_list)
    filtered_traces = []

    for index, trace in enumerate(study_list):
        trace["filter_study_list_index"] = index
        if rollup is not None:
            try:
                trace.update(rollup.loc[trace["name"]].to_dict())
            except KeyError:
                pass

        if float(trace["weight"]) < args.min_weight:
            continue

        exclude = any(e.search(trace[args.match]) for e in excludes)
        if exclude:
            continue

        include = not includes or any(i.search(trace[args.match]) for i in includes)
        if include:
            filtered_traces.append(trace)

    # Include only enough traces from each workload so that each workload has
    # enough traces to add up to args.workload_weights_sum total weight. After
    # identifying which traces will be included from each workload, scale the
    # weights of the included traces so each workload's weights still add up to
    # the same total weight as before (usually 1.0).
    if args.workload_weights_sum < sys.float_info.max:
        workload_traces = gather_workload_traces(filtered_traces)
        weighted_filtered_traces = []
        for traces in workload_traces.values():
            included_weight = 0.0
            for trace in sorted(traces, key=lambda t: float(t["weight"]), reverse=True):
                if included_weight < args.workload_weights_sum:
                    weighted_filtered_traces.append(trace)
                    included_weight += float(trace["weight"])
    else:
        weighted_filtered_traces = filtered_traces

    # Assign by index
    if args.assign_by_index:
        assign_field_values(args.assign_by_index, weighted_filtered_traces)
    if args.assign_by_index_file:
        with open(args.assign_by_index_file, "r") as af:
            assign_field_values([l.split() for l in af.readlines()], weighted_filtered_traces)

    # Rename fields
    if args.rename:
        rename_field_values(args.rename, weighted_filtered_traces)

    # Normalize all weights within a workload to sum to 1.0
    if args.normalize or args.workload_weights_sum < sys.float_info.max:
        normalize_weights_within_workload(weighted_filtered_traces)

    # Update filenames to be absolute if we're not creating a study list in the
    # same directory as the input study list
    input_dirname = dirname(abspath(realpath(args.input_list)))
    if input_dirname != dirname(abspath(realpath(args.output_list))):
        for trace in weighted_filtered_traces:
            for field, value in trace.items():
                if (field in ("file",) or field.endswith("_file")) and "/" in value:
                    trace[field] = join(input_dirname, trace[field])

    if args.sort_by:
        # Sort by field
        output_traces = sorted(
            weighted_filtered_traces,
            key=lambda t: t.get(args.sort_by, 0),
            reverse=bool(args.sort_descending),
        )
    else:
        # Re-sort traces so they are in the same order as the original traces list
        output_traces = sorted(weighted_filtered_traces, key=lambda t: t["filter_study_list_index"])

    # Remove fields not in original fields
    for trace in output_traces:
        remove = set(trace.keys()) - keep_fields
        for key in remove:
            del trace[key]

    with open(args.output_list, "w") as output_study_list:
        json.dump(output_traces, output_study_list, indent=4)

    logging.info(
        f"Read {original_number_of_traces} traces from input study list, wrote {len(output_traces)} traces to {args.output_list}"
    )


if __name__ == "__main__":
    setup_logging()
    main(parse_args())
