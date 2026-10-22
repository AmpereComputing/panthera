#!/usr/bin/env python
# Copyright (c) 2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import argparse
import cProfile
import functools
from pstats import SortKey, Stats


def profile(filename):
    """
    This method is intended to be used as a decorator to profile a function.
    The assumption is that the method will only be invoked once, because the
    file the profile is saved to will be overwritten each time.

    usage example:

        from panthera.profile import profile

        @profile("filename.profile")
        def my_method():
           ...
    """

    def actual_decorator(func):
        @functools.wraps(func)
        def wrapper(*args, **kwargs):
            profile = cProfile.Profile()
            profile.enable()
            ret = func(*args, **kwargs)
            profile.disable()
            profile.dump_stats(filename)
            return ret

        return wrapper

    return actual_decorator


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("filename", help="The cProfile filename to display")
    parser.add_argument("--top", type=int, default=20, help="How many profiling units to display")
    parser.add_argument(
        "--no-strip-directories", action="store_true", help="Do not strip directory names from the display"
    )
    parser.add_argument(
        "--sort",
        default=SortKey.TIME,
        choices=[SortKey.NAME, SortKey.TIME, SortKey.CUMULATIVE, SortKey.FILENAME],
        help="How to sort the profiling units",
    )
    args = parser.parse_args()

    p = Stats(args.filename)

    if not args.no_strip_directories:
        p = p.strip_dirs()

    p.sort_stats(args.sort).print_stats(args.top)
