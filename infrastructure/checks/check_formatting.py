#!/usr/bin/env python3
# Copyright (c) 2019 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Check (and optionally correct) source file formatting, headers, and footers.
"""

import argparse
import logging as log
import os
import re
import subprocess
from datetime import date

log.basicConfig(format="%(levelname)s: %(message)s")
log.root.setLevel(log.INFO)


def main():
    args = parse_args()
    errors = 0

    for file in find_source_files(args.files):
        if not args.no_format:
            errors += check_formatting(file, args.fix)
        if not args.no_headers:
            errors += check_header(file, args.fix)
        if not args.no_footers:
            errors += check_footer(file, args.fix)

    exit(errors)


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--fix", action="store_true", help="Fix errors in addition to reporting them")
    parser.add_argument("--no-format", action="store_true", help="Disable check/fix of formatting")
    parser.add_argument("--no-headers", action="store_true", help="Disable check/fix of headers")
    parser.add_argument("--no-footers", action="store_true", help="Disable check/fix of footers")
    parser.add_argument("files", help="Files or directories to process", nargs="*", default=["src/ampere"])
    return parser.parse_args()


def find_source_files(starting_points):
    for name in starting_points:
        if os.path.isfile(name):
            if is_source_file(name):
                yield name
            else:
                log.fatal(f"{name} is not named like a C++ source file")
        else:
            yield from find_sources_in_dir(name)


def is_source_file(file):
    _, suffix = os.path.splitext(file)
    return suffix in [".cc", ".cpp", ".h"]


def find_sources_in_dir(top):
    for path, _, files in os.walk(top):
        for file in files:
            if is_source_file(file):
                yield os.path.join(path, file)


def check_formatting(file, fix):
    badly_formatted = 0
    with open(f"{file}.formatted", "w") as formatted:
        subprocess.check_call(["clang-format", file], stdout=formatted, stderr=subprocess.DEVNULL)
    status = subprocess.run(["cmp", "--quiet", file, f"{file}.formatted"])
    if status.returncode != 0:
        if fix:
            log.info(f"{file} is not properly formatted (fixed)")
            os.rename(f"{file}.formatted", file)
        else:
            log.error(f"{file} is not properly formatted")
            badly_formatted += 1
            os.remove(f"{file}.formatted")
    return badly_formatted


def check_header(file, fix):
    lines = open(file).readlines()
    header_lines = "".join(lines[:3])
    header_re = """//
// Copyright \(c\) ([0-9]+)(-[0-9]+)? Ampere Computing. All rights reserved.
//
"""
    this_year = date.today().year
    m = re.match(header_re, header_lines)
    if not m:
        if fix:
            log.info(f"{file} has an improper header (fixed)")
            replace = all([l[:2] == "//" for l in header_lines])
            new_header = ["//\n", f"// Copyright (c) {this_year} Ampere Computing. All rights reserved.\n", "//\n"]
            if replace:
                lines[:3] = new_header
            else:
                lines = new_header + lines
            with open(file, "w") as f:
                f.writelines(lines)
        else:
            log.error(f"{file} has an improper header")
            return 1
    else:
        # Check if the copyright date reaches this year
        from_year = int(m.group(1))
        to_year = int(m.group(2)[1:]) if m.group(2) else from_year

        if to_year != this_year:
            if fix:
                log.info(f"{file} has an out of date copyright (fixed)")
                new_header = [
                    "//\n",
                    f"// Copyright (c) {from_year}-{this_year} Ampere Computing. All rights reserved.\n",
                    "//\n",
                ]
                lines[:3] = new_header
                with open(file, "w") as f:
                    f.writelines(lines)
            else:
                log.warn(f"{file} has an out of date copyright")
    return 0


def check_footer(file, fix):
    lines = open(file).readlines()
    footer_lines = lines[-4:]
    proper_footer = ["\n", "/*\n", " * vim: sw=4:ts=4:et:si:cindent:cino=\\:0,(0,g0,t0:\n", " */\n"]
    if footer_lines != proper_footer:
        if fix:
            log.info(f"{file} has an improper footer (fixed)")
            replace = [l[:2] for l in footer_lines] == ["", "/*", " *", " */"]
            if replace:
                lines[-4:] = proper_footer
            else:
                if lines[-1][-1] != "\n":
                    lines[-1] += "\n"
                lines += proper_footer
            with open(file, "w") as f:
                f.writelines(lines)
        else:
            log.error(f"{file} has an improper footer")
            return 1
    return 0


if __name__ == "__main__":
    main()
