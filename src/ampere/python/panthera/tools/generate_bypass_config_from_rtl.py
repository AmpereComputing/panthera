# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Generate bypass JSON config from RTL."""

import argparse
import json
import re
import sys


def parse_args():
    parser = argparse.ArgumentParser(description="Generate FSU Bypass JSON from RTL")
    parser.add_argument(
        "input",
        type=argparse.FileType("r"),
        help="RTL file specifying latencies per producer unit and level of bypass (usually FsuPkg.sv)",
    )
    parser.add_argument(
        "-b",
        "--base-latency",
        type=int,
        default=2,
        help="Base bypass latency (to add to RTL value)",
    )
    parser.add_argument(
        "-e",
        "--exclude",
        action="append",
        default=["Unused"],
        help="Exclude lines with phrase",
    )
    parser.add_argument("-o", "--output", type=argparse.FileType("w"), default=sys.stdout)

    return parser.parse_args()


def mask_to_latency(value: str, offset: int):
    return value.count("1") + offset


def main(args):
    bypass_latency = re.compile(
        r"(?P<unit>[A-Z]+)(?P<unit_suffix>\d)?_(?P<level>[A-Z]+)_LATENCY +\= .*?'b(?P<bypass>[01]+).*?;"
    )
    config = {}
    levels = {
        "LCL": "LOCAL",
        "NRM": "NORMAL",
        "RND": "UNROUNDED",
    }
    unit_keys = {"unit", "unit_suffix"}
    level_keys = {"level", "bypass"}

    with args.input as f:
        for line in f:
            if line.lstrip().startswith("//"):
                continue

            if any(phrase in line for phrase in args.exclude):
                continue

            if m := bypass_latency.search(line):
                values = {k: v for k, v in m.groupdict().items() if v is not None and k in level_keys}
                bypass = mask_to_latency(values["bypass"], args.base_latency)
                level = levels.get(values["level"].upper(), values["level"].upper())

                unit_values = {k: v for k, v in m.groupdict().items() if v is not None and k in unit_keys}
                if "unit_suffix" in unit_values:
                    unit_values["unit_suffix"] = int(unit_values["unit_suffix"])

                name = f"{unit_values['unit']}{unit_values.get('unit_suffix', '')}"
                if name not in config:
                    unit_values["levels"] = {}
                    config[name] = unit_values

                config[name]["levels"][level] = bypass

    with args.output as of:
        json.dump({"latencies": config}, of, indent=4)


if __name__ == "__main__":
    main(parse_args())
