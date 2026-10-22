# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import re

_time_re = re.compile(r"^([\d.]+)\s*(\w*)$")
_time_suffixes = {
    "s": 0,
    "ms": -3,
    "us": -6,
    "ns": -9,
    "ps": -12,
    "fs": -15,
}


def time_from_string(time_string: str, normalize_to="ps") -> int:
    """Parse time from string and normalize to ps."""
    m = _time_re.match(time_string)
    seconds = int(m.group(1))
    if suffix := m.group(2):
        seconds *= 10 ** _time_suffixes.get(suffix, 0)

    return int(seconds / 10 ** _time_suffixes.get(normalize_to, 0))
