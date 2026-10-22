# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


def accuracy(hit_cacheline, hit_ofb, launch):
    """Prefetcher accuracy."""
    if launch.empty:
        return 0
    return (hit_cacheline + hit_ofb) / launch
