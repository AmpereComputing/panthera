# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


def accuracy(hit, launch, drop):
    """Prefetcher accuracy."""
    if launch.empty:
        return 0
    return hit / (launch - drop)
