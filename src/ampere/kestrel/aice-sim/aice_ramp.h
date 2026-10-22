//
// Copyright (c) 2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once
#include <vector>
namespace ampere::kestrel::aice {
// Emit a vector showing the supercell activity for the specified
// matrix multiplication.  Need to add this element-wise (time
// shifted) with overlapping matmula to get final profile.
// Element values between 0..256.
std::vector<int> mar_ramp(unsigned mm, unsigned nn);
} // namespace ampere::kestrel::aice
