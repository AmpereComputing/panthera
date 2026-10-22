# Copyright (c) 2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Report performance outliers locally."""

from .reports.triage_perf import main, parse_args

if __name__ == "__main__":
    main(parse_args())
