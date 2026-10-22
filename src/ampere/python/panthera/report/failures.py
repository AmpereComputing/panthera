# Copyright (c) 2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Summarize study failures locally."""

from .reports.failures import main, parse_args

if __name__ == "__main__":
    main(parse_args())
