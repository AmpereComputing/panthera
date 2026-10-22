//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once
#include <cstdint>
namespace ampere::core::srt_div {
unsigned f64_div_radix_4_iterations(const std::uint64_t numerator, const std::uint64_t denominator);
unsigned f32_div_radix_4_iterations(const std::uint32_t numerator, const std::uint32_t denominator);
unsigned u64_div_radix_4_iterations(const std::uint64_t numerator, const std::uint64_t denominator);
unsigned u32_div_radix_4_iterations(const std::uint32_t numerator, const std::uint32_t denominator);
unsigned s64_div_radix_4_iterations(const std::int64_t numerator, const std::int64_t denominator);
unsigned s32_div_radix_4_iterations(const std::int32_t numerator, const std::int32_t denominator);

unsigned f64_div_radix_16_iterations(const std::uint64_t numerator, const std::uint64_t denominator);
unsigned f32_div_radix_16_iterations(const std::uint32_t numerator, const std::uint32_t denominator);
unsigned u64_div_radix_16_iterations(const std::uint64_t numerator, const std::uint64_t denominator);
unsigned u32_div_radix_16_iterations(const std::uint32_t numerator, const std::uint32_t denominator);
unsigned s64_div_radix_16_iterations(const std::int64_t numerator, const std::int64_t denominator);
unsigned s32_div_radix_16_iterations(const std::int32_t numerator, const std::int32_t denominator);

unsigned f64_div_radix_64_iterations(const std::uint64_t numerator, const std::uint64_t denominator);
unsigned f32_div_radix_64_iterations(const std::uint32_t numerator, const std::uint32_t denominator);
unsigned u64_div_radix_64_iterations(const std::uint64_t numerator, const std::uint64_t denominator);
unsigned u32_div_radix_64_iterations(const std::uint32_t numerator, const std::uint32_t denominator);
unsigned s64_div_radix_64_iterations(const std::int64_t numerator, const std::int64_t denominator);
unsigned s32_div_radix_64_iterations(const std::int32_t numerator, const std::int32_t denominator);

} // namespace ampere::core::srt_div
