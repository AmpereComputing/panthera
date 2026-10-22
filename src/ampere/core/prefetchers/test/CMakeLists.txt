# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

# FIXME: ideally these should be a separate test, but there's some testing
# dependencies *only* in core_unit so...
target_sources(core_unit PRIVATE
    test_l1p_pcp.cc
    test_l1p_gretch.cc
    test_multiple.cc
    test_oracle.cc)
