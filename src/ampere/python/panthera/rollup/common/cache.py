# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from . import nanoseconds


def bandwidth(metric, metrics, line_size=64):
    """Bandwidth (per nanosecond)."""
    return line_size * metric / nanoseconds(metrics)
