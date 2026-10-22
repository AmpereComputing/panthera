//
// Copyright (c) 2022-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <utility>
#include <cstdlib>
#include "ampere/core/ixu.h"
#include "ampere/core/div_supp.h"
#include "ampere/util/bits.h"
#include "ampere/core/early_exit.enum.h"
#include "ampere/core/ixu_srt_div.h"

// #define DEBUG_DIV_LATENCY
#if defined(DEBUG_DIV_LATENCY)
#define DBG_DLAT(format_str, ...) fmt::print(stderr, format_str "\n", ##__VA_ARGS__)
#else
#define DBG_DLAT(format_str, ...)
#endif

// #define DEBUG_DIV_EARLY_EXIT
#if defined(DEBUG_DIV_EARLY_EXIT)
#define DBG_DIVEE(format_str, ...) fmt::print(stderr, format_str "\n", ##__VA_ARGS__)
#else
#define DBG_DIVEE(format_str, ...)
#endif

// #define DEBUG_DIV_EARLY_EXIT2
#if defined(DEBUG_DIV_EARLY_EXIT2)
#define DBG_DIVEE2(format_str, ...) fmt::print(stderr, format_str "\n", ##__VA_ARGS__)
#else
#define DBG_DIVEE2(format_str, ...)
#endif

namespace ampere::core {

static uint64_t get_fp_sign(uint64_t x, int size)
{
    Bitfield b{x};

    switch (size) {
    case 64:
        return b.extract_range(63, 63);
    case 32:
        return b.extract_range(31, 31);
    case 16:
        return b.extract_range(15, 15);
    default:
        AMPERE_ERROR("Illegal size in get_fp_sign");
        return 0;
    }
}

static uint64_t get_fp_exp(uint64_t x, int size)
{
    Bitfield b{x};

    switch (size) {
    case 64:
        return b.extract_range(62, 52);
    case 32:
        return b.extract_range(30, 23);
    case 16:
        return b.extract_range(14, 10);
    default:
        AMPERE_ERROR("Illegal size in get_fp_exp");
        return 0;
    }
}

static uint64_t get_fp_frac(uint64_t x, int size)
{
    Bitfield b{x};

    switch (size) {
    case 64:
        return b.extract_range(51, 0);
    case 32:
        return b.extract_range(22, 0);
    case 16:
        return b.extract_range(9, 0);
    default:
        AMPERE_ERROR("Illegal size in get_fp_frac");
        return 0;
    }
}

static bool is_fp_zero(uint64_t x, int size)
{
    uint64_t exp = get_fp_exp(x, size);
    uint64_t frac = get_fp_frac(x, size);

    return exp == 0 && frac == 0;
}

static bool is_fp_infinity(uint64_t x, int size)
{
    uint64_t exp = get_fp_exp(x, size);
    uint64_t frac = get_fp_frac(x, size);

    switch (size) {
    case 64:
        return (frac == 0) && exp == (1 << 11) - 1;
    case 32:
        return (frac == 0) && exp == (1 << 8) - 1;
    case 16:
        return (frac == 0) && exp == (1 << 5) - 1;
    default:
        AMPERE_ERROR("Illegal size in is_fp_infinity");
        return false;
    }
}

static bool is_fp_nan(uint64_t x, int size)
{
    uint64_t exp = get_fp_exp(x, size);
    uint64_t frac = get_fp_frac(x, size);

    switch (size) {
    case 64:
        return (frac != 0) && exp == (1 << 11) - 1;
    case 32:
        return (frac != 0) && exp == (1 << 8) - 1;
    case 16:
        return (frac != 0) && exp == (1 << 5) - 1;
    default:
        AMPERE_ERROR("Illegal size in is_fp_nan");
        return false;
    }
}

static bool is_fp_denorm(uint64_t x, int size)
{
    uint64_t exp = get_fp_exp(x, size);
    uint64_t frac = get_fp_frac(x, size);

    return exp == 0 && frac != 0;
}

static bool is_fp_power2(uint64_t x, int size)
{
    uint64_t exp = get_fp_exp(x, size);
    uint64_t frac = get_fp_frac(x, size);

    return exp != 0 && frac == 0;
}

void IXU::divide_properties_init(const Uop& uop)
{
    generic_vector_pair_t nd = idiv_value_fixer(uop);
    const EarlyExitReason idiv_ee = int_early_exit_config(uop, nd);
    if (idiv_early_exit_latency && idiv_ee != EarlyExitReason::Invalid) {
        uop->set_early_exit_reason(idiv_ee);
        uop->set_idiv_early_exit(true);
        uop->div_latency = uop->is_vector_idiv() ? idiv_vector_early_exit_latency : idiv_early_exit_latency;
    }
    // only try idiv variable latency if not already early exit as denoted
    // by nonzero div_latency.
    if (idiv_use_value_derived_latency && uop->div_latency == 0 && uop->is_idiv()) {
        uop->div_latency = idiv_variable_latency_config(uop, nd);
    }
    const EarlyExitReason fdiv_ee = fp_early_exit_config(uop);
    if (fdiv_early_exit_latency && fdiv_ee != EarlyExitReason::Invalid) {
        uop->set_early_exit_reason(fdiv_ee);
        uop->set_fdiv_early_exit(true);
        uop->div_latency = fdiv_early_exit_latency; // same for fdiv and fsqrt
    }
    // only try fdiv variable latency if not already early exit as denoted
    // by nonzero div_latency.
    if (fdiv_use_value_derived_latency && uop->div_latency == 0 && uop->is_fdiv() && !uop->is_fdiv16()) {
        uop->div_latency = fdiv_variable_latency_config(uop);
    }
}

unsigned IXU::f64_div_iterations(std::uint64_t numerator, std::uint64_t denominator) const
{
    switch (divide_radix) {
    case 4:
        return srt_div::f64_div_radix_4_iterations(numerator, denominator);
    case 16:
        return srt_div::f64_div_radix_16_iterations(numerator, denominator);
    case 64:
        return srt_div::f64_div_radix_64_iterations(numerator, denominator);
    }
    return 0;
}
unsigned IXU::f32_div_iterations(std::uint32_t numerator, std::uint32_t denominator) const
{
    switch (divide_radix) {
    case 4:
        return srt_div::f32_div_radix_4_iterations(numerator, denominator);
    case 16:
        return srt_div::f32_div_radix_16_iterations(numerator, denominator);
    case 64:
        return srt_div::f32_div_radix_64_iterations(numerator, denominator);
    }
    return 0;
}

unsigned IXU::u64_div_iterations(std::uint64_t numerator, std::uint64_t denominator) const
{
    switch (divide_radix) {
    case 4:
        return srt_div::u64_div_radix_4_iterations(numerator, denominator);
    case 16:
        return srt_div::u64_div_radix_16_iterations(numerator, denominator);
    case 64:
        return srt_div::u64_div_radix_64_iterations(numerator, denominator);
    }
    return 0;
}

unsigned IXU::u32_div_iterations(std::uint32_t numerator, std::uint32_t denominator) const
{
    switch (divide_radix) {
    case 4:
        return srt_div::u32_div_radix_4_iterations(numerator, denominator);
    case 16:
        return srt_div::u32_div_radix_16_iterations(numerator, denominator);
    case 64:
        return srt_div::u32_div_radix_64_iterations(numerator, denominator);
    }
    return 0;
}
unsigned IXU::s64_div_iterations(std::int64_t numerator, std::int64_t denominator) const
{
    switch (divide_radix) {
    case 4:
        return srt_div::s64_div_radix_4_iterations(numerator, denominator);
    case 16:
        return srt_div::s64_div_radix_16_iterations(numerator, denominator);
    case 64:
        return srt_div::s64_div_radix_64_iterations(numerator, denominator);
    }
    return 0;
}

unsigned IXU::s32_div_iterations(std::int32_t numerator, std::int32_t denominator) const
{
    switch (divide_radix) {
    case 4:
        return srt_div::s32_div_radix_4_iterations(numerator, denominator);
    case 16:
        return srt_div::s32_div_radix_16_iterations(numerator, denominator);
    case 64:
        return srt_div::s32_div_radix_64_iterations(numerator, denominator);
    }
    return 0;
}

static std::uint32_t fn_bits_per_iteration(unsigned divide_radix)
{
    std::uint32_t bpi = 2;
    switch (divide_radix) {
    case 4:
        bpi = 2;
        break;
    case 16:
        bpi = 4;
        break;
    case 64:
        bpi = 6;
        break;
    }
    return bpi;
}

static std::uint32_t fdiv_max_iterations(unsigned esize_bits, unsigned divide_radix)
{
    const std::uint32_t fraction_bits = (esize_bits == 64) ? (52 + 1) : (23 + 1); // include implicit 1.
    const std::uint32_t guard_bit = 1;
    const std::uint32_t round_bit = 1;
    const std::uint32_t max_bits = fraction_bits + guard_bit + round_bit;
    const std::uint32_t bits_per_iteration = fn_bits_per_iteration(divide_radix);
    // round up
    return (max_bits + bits_per_iteration - 1) / bits_per_iteration;
}

std::uint64_t IXU::fdiv_variable_latency_config(const Uop& uop) const
{
    // return the new latency

    const int elements = uop->uop_info()->vector_element_count;
    const int esize = uop->uop_info()->vector_element_size;

    auto anumerator = uop->vfp(uop->reg_src[0]); // fp16, fp32, fp64
    auto adenominator = uop->vfp(uop->reg_src[1]);
    if (uop->is_reverse_divide()) {
        std::swap(adenominator, adenominator);
    }

    // copy to container that can extract any width
    const ampere::archx::vector_t vnumerator{{anumerator[0], anumerator[1]}};
    const ampere::archx::vector_t vdenominator{{adenominator[0], adenominator[1]}};

    archx::pred_reg_t pval = 0xFFFF;
    if (uop->is_sve()) {
        pval = uop->pred(uop->governing_predicate_reg());
        AMPERE_ASSERT(pval != 0, "all predicated off would be early-exit");
    }

    std::vector<unsigned> iterations;
    bool all_normal_numbers = true;
    for (int e = 0; e < elements; e++) {
        if (pval == 0xFFFF || archx::get_pred_bit(pval, e, esize)) { // governing predicate bit == true
            if (esize == 32) {
                const std::uint32_t numerator = vnumerator.u32[e];
                const std::uint32_t denominator = vdenominator.u32[e];
                if (all_normal_numbers && (is_fp_denorm(numerator, 32) || is_fp_denorm(denominator, 32))) {
                    all_normal_numbers = false;
                }
                if (numerator == 0 || denominator == 0) {
                    iterations.push_back(0);
                } else {
                    iterations.push_back(f32_div_iterations(numerator, denominator));
                }
            } else if (esize == 64) {
                const std::uint64_t numerator = vnumerator.u64[e];
                const std::uint64_t denominator = vdenominator.u64[e];
                if (all_normal_numbers && (is_fp_denorm(numerator, 64) || is_fp_denorm(denominator, 64))) {
                    all_normal_numbers = false;
                }
                if (numerator == 0 || denominator == 0) {
                    iterations.push_back(0);
                } else {
                    iterations.push_back(f64_div_iterations(numerator, denominator));
                }
            }
        }
    }
    auto max_iterations = std::max_element(iterations.begin(), iterations.end());
    const std::uint32_t srt_iterations = *max_iterations;
    AMPERE_ASSERT(srt_iterations > 0, "SRT div iterations must be > 0");

    // variable div has 2 cycle longer back end processing if less
    // than full number of iterations. Therefore we limit the number
    // of iterations for a variable latency divide such that we don't
    // exceed what the full latency divide would have been. The
    // variable latency div also has a cycle for detecting partial
    // remainder == 0 that the full latency div does not have. That
    // means for f32, if we would do >=4 variable iterations, we just
    // do full latency.  And for f64, if we would do >= 10 variable
    // iterations, we just do full latency.

    const std::uint32_t fx00 = 1; // decoding
    // we shave a cycle if the the numbers are not denormals
    const std::uint32_t prescale = (fdiv_normal_numbers_optimization && all_normal_numbers) ? 0 : 1;
    const std::uint32_t postscale = 1;
    const std::uint32_t round = 1;
    const std::uint32_t prem0 = 1; // test for partial remainder == 0 (shows up as the last "LOOP*" in waves)
    // 'scale' is a new front-end cycle for radix64
    const std::uint32_t scale = (divide_radix == 64) ? 1 : 0;
    const std::uint32_t front_end = fx00 + prescale + scale;
    const std::uint32_t back_end = postscale + round;
    const std::uint32_t variable_back_end = 2 + back_end;
    const std::uint32_t gen_max_iterations = fdiv_max_iterations(esize, divide_radix);

    // max iterations has shorter back end and no prem0 cycle
    const std::uint32_t max_cycles = front_end + gen_max_iterations + back_end;
    if (divide_fixed_latency) {
        return max_cycles;
    }

    // the extra_srt_div_latency (ixu parameter) is 2 cycles on Connor
    // and is part of the 5 cycle delay before div_wb issues.
    // "div-almost-done" is sent in the first "variable back end"
    // cycle. The div_wb issues 5 cycles later.
    const std::uint32_t variable_cycles =
        front_end + srt_iterations + prem0 + variable_back_end + extra_srt_div_latency;
    const std::uint32_t cycles = std::min(max_cycles, variable_cycles);
    DBG_DLAT("divlat srt_iter: {} cycles: {} esize: {} max_cycles: {} ", srt_iterations, cycles, esize, max_cycles);
    return cycles;
}

std::uint64_t IXU::idiv_variable_latency_config(const Uop& uop, const generic_vector_pair_t& nd) const
{
    std::uint64_t new_latency{0};
    if (uop->is_scalar_idiv())
        new_latency = scalar_idiv_iterations_config(uop, nd);
    else if (uop->is_vector_idiv()) // sve
        new_latency = vector_idiv_iterations_config(uop, nd);
    return new_latency;
}

static generic_vector_pair_t fixup_scalar_for_idiv_checks(const Uop& uop)
{
    // For scalar idiv, the values in the traces are 64b. We must
    // properly truncate & sign extend the bit31 for those cases
    const std::uint64_t numerator = uop->gpr(uop->ainst()->divide_src(0));
    const std::uint64_t denominator = uop->gpr(uop->ainst()->divide_src(1));
    const bool is_unsigned = uop->is_scalar_udiv();
    const bool size_32b = uop->is_scalar_idiv32();
    generic_vector_pair_t nd;
    if (is_unsigned) {
        if (size_32b) {
            // zero extend bit31
            nd.numerator.u64[0] = static_cast<std::uint32_t>(numerator);
            nd.denominator.u64[0] = static_cast<std::uint32_t>(denominator);
            DBG_DIVEE("u32 num  {:016x} -> {:016x}", numerator, nd.numerator.u64[0]);
            DBG_DIVEE("u32 den  {:016x} -> {:016x}", denominator, nd.denominator.u64[0]);
        } else {
            nd.numerator.u64[0] = numerator;
            nd.denominator.u64[0] = denominator;
            DBG_DIVEE("u64 num  {:016x}", numerator);
            DBG_DIVEE("u64 den  {:016x}", denominator);
        }
    } else {
        if (size_32b) {
            // treat at int32 and sign extend
            nd.numerator.i64[0] = static_cast<std::int32_t>(numerator);
            nd.denominator.i64[0] = static_cast<std::int32_t>(denominator);
            DBG_DIVEE("i32 num  {:016x} -> {:016x} {}", numerator, nd.numerator.i64[0], nd.numerator.i64[0]);
            DBG_DIVEE("i32 den  {:016x} -> {:016x} {}", denominator, nd.denominator.i64[0], nd.denominator.i64[0]);
        } else {
            // convert u64 to i64
            nd.numerator.i64[0] = static_cast<std::int64_t>(numerator);
            nd.denominator.i64[0] = static_cast<std::int64_t>(denominator);
            DBG_DIVEE("i64 num  {:016x} -> {:016x} {}", numerator, nd.numerator.i64[0], nd.numerator.i64[0]);
            DBG_DIVEE("i64 den  {:016x} -> {:016x} {}", denominator, nd.denominator.i64[0], nd.denominator.i64[0]);
        }
    }
    return nd;
}

static generic_vector_pair_t fixup_vector_for_idiv_checks(const Uop& uop)
{
    const int elements = uop->uop_info()->vector_element_count;
    const int esize = uop->uop_info()->vector_element_size;
    const bool is_unsigned = uop->is_vector_udiv();

    auto anumerator = uop->vfp(uop->reg_src[0]);   // 128b result
    auto adenominator = uop->vfp(uop->reg_src[1]); // 128b result

    if (uop->is_reverse_divide()) { // sve has reverse divides
        std::swap(adenominator, adenominator);
    }
    // copy to container that can extract any width
    const ampere::archx::vector_t vnumerator{{anumerator[0], anumerator[1]}};
    const ampere::archx::vector_t vdenominator{{adenominator[0], adenominator[1]}};

    archx::pred_reg_t pval = 0xFFFF;
    if (uop->is_sve()) {
        pval = uop->pred(uop->governing_predicate_reg());
    }

    generic_vector_pair_t nd;
    for (int elem = 0; elem < elements; elem++) {
        if (pval == 0xFFFF || archx::get_pred_bit(pval, elem, esize)) { // governing predicate bit == true
            if (is_unsigned) {
                nd.numerator.u64[elem] = vnumerator.get_vector_element_u64(elem, esize);
                nd.denominator.u64[elem] = vdenominator.get_vector_element_u64(elem, esize);
                DBG_DIVEE("vu64 num {} {:016x}", elem, nd.numerator.u64[elem]);
                DBG_DIVEE("vu64 den {} {:016x}", elem, nd.denominator.u64[elem]);

            } else {
                nd.numerator.i64[elem] = vnumerator.get_vector_element_i64(elem, esize);
                nd.denominator.i64[elem] = vdenominator.get_vector_element_i64(elem, esize);
                DBG_DIVEE("vi64 num {}  {:016x} {}", elem, nd.numerator.i64[elem], nd.numerator.i64[elem]);
                DBG_DIVEE("vi64 den {}  {:016x} {}", elem, nd.denominator.i64[elem], nd.denominator.i64[elem]);
            }
        }
    }
    return nd;
}

generic_vector_pair_t IXU::idiv_value_fixer(const Uop& uop) const
{
    // the values in the traces are always 64b, even for 32b operands.
    // this function sign extends the 32b signed integers properly and
    // zero extends 32b unsigned integers properly, storing them in
    // 64b containers. The consumers of the generic_vector_pair_t can
    // then work with the only 64b values.
    generic_vector_pair_t nd;
    if (uop->uop_info()->is_idiv) {
        if (uop->is_scalar_idiv()) {
            nd = fixup_scalar_for_idiv_checks(uop);
        } else if (vector_idiv_early_exit) {
            nd = fixup_vector_for_idiv_checks(uop);
        }
    }
    return nd;
}

EarlyExitReason IXU::int_early_exit_config(const Uop& uop, const generic_vector_pair_t& nd) const
{
    if (uop->is_idiv()) {
        if (uop->is_scalar_idiv()) {
            const int elements = 1;
            return idiv_vector_early_exit_config(uop, nd, elements);
        } else if (vector_idiv_early_exit) {
            const int elements = uop->uop_info()->vector_element_count;
            return idiv_vector_early_exit_config(uop, nd, elements);
        }
    }
    return EarlyExitReason::Invalid;
}

EarlyExitReason IXU::fp_early_exit_config(const Uop& uop) const
{
    const int elements = uop->uop_info()->vector_element_count;

    if (uop->is_fdiv()) {
        if ((elements == 1 && scalar_fdiv_fsqrt_early_exit) || (elements > 1 && vector_fdiv_early_exit)) {
            return fdiv_early_exit_config(uop);
        }
    }

    if (uop->is_fsqrt()) {
        if ((elements == 1 && scalar_fdiv_fsqrt_early_exit) || (elements > 1 && vector_sqrt_early_exit)) {
            return fsqrt_early_exit_config(uop);
        }
    }
    return EarlyExitReason::Invalid;
}

static bool is_fp_even_power_two(std::uint64_t x, int size)
{
    uint64_t exp = get_fp_exp(x, size);
    uint64_t frac = get_fp_frac(x, size);

    // EVEN powers of two have ODD exponents due to odd bias values
    return frac == 0 && (exp & 1) == 1;
}

static bool is_fp_odd_power_two(std::uint64_t x, int size)
{
    uint64_t exp = get_fp_exp(x, size);
    uint64_t frac = get_fp_frac(x, size);

    // ODD powers of two have EVEN exponents due to odd bias values
    return frac == 0 && (exp & 1) == 0;
}

EarlyExitReason IXU::test_fsqrt_early_exit(std::uint64_t radicand, int size) const
{
    if (is_fp_zero(radicand, size)) {
        return EarlyExitReason::FPSqrtZero;
    }

    // FZ/FZ16 handling bug for radicand input.
    if (is_fp_denorm(radicand, size)) {
        return EarlyExitReason::FPSqrtDenorm;
    }
    if (is_fp_nan(radicand, size)) {
        return EarlyExitReason::FPSqrtNan;
    }
    if (is_fp_infinity(radicand, size)) {
        return EarlyExitReason::FPSqrtInfinity;
    }
    if (get_fp_sign(radicand, size)) {
        return EarlyExitReason::FPSqrtNegaitve;
    }
    if (is_fp_even_power_two(radicand, size)) {
        return EarlyExitReason::FPSqrtEvenPower2;
    }
    if (fsqrt_early_exit_odd_powers_of_two && is_fp_odd_power_two(radicand, size)) { // connor onwards
        return EarlyExitReason::FPSqrtOddPower2;
    }
    return EarlyExitReason::Invalid;
}

EarlyExitReason IXU::fsqrt_early_exit_config(const Uop& uop) const
{
    const int elements = uop->uop_info()->vector_element_count;
    const int esize = uop->uop_info()->vector_element_size;

    auto aradicand = uop->vfp(uop->reg_src[0]);
    // copy to container that can extract any width
    const ampere::archx::vector_t vradicand{{aradicand[0], aradicand[1]}};

    archx::pred_reg_t pval = 0xFFFF;
    if (uop->is_sve()) {
        pval = uop->pred(uop->governing_predicate_reg());
        if (archx::pred_all_zero(pval, esize)) {
            return EarlyExitReason::FPSqrtAllPredicatedOff;
        }
    }

    // for vector ops, take last reason as the early exit reason
    EarlyExitReason ee_reason{EarlyExitReason::Invalid};
    for (int e = 0; e < elements; e++) {
        if (pval == 0xFFFF || archx::get_pred_bit(pval, e, esize)) { // governing predicate bit == true
            const std::uint64_t radicand = vradicand.get_vector_element_u64(e, esize);
            ee_reason = test_fsqrt_early_exit(radicand, esize);
            if (ee_reason == EarlyExitReason::Invalid) { // not early exit
                break;
            }
        }
    }
    return ee_reason;
}

static EarlyExitReason test_fdiv_early_exit(std::uint64_t numerator, std::uint64_t denominator, int size)
{
    if (is_fp_zero(numerator, size)) {
        return EarlyExitReason::FPDivNumZero;
    }
    if (is_fp_zero(denominator, size)) {
        return EarlyExitReason::FPDivDenomZero;
    }
    // FZ/FZ16 handling bug for numerator / denominator.
    if (is_fp_denorm(numerator, size)) {
        return EarlyExitReason::FPDivNumDenorm;
    }
    if (is_fp_denorm(denominator, size)) {
        return EarlyExitReason::FPDivDenomDenorm;
    }
    if (is_fp_infinity(numerator, size)) {
        return EarlyExitReason::FPDivNumInfinity;
    }
    if (is_fp_infinity(denominator, size)) {
        return EarlyExitReason::FPDivDenomInfinity;
    }
    if (is_fp_nan(numerator, size)) {
        return EarlyExitReason::FPDivNumNan;
    }
    if (is_fp_nan(denominator, size)) {
        return EarlyExitReason::FPDivDenomNan;
    }
    if (is_fp_power2(denominator, size)) {
        return EarlyExitReason::FPDivDenomPower2;
    }
    return EarlyExitReason::Invalid;
}

EarlyExitReason IXU::fdiv_early_exit_config(const Uop& uop)
{
    const int elements = uop->uop_info()->vector_element_count;
    const int esize = uop->uop_info()->vector_element_size;

    auto anumerator = uop->vfp(uop->reg_src[0]); // fp16, fp32, fp64
    auto adenominator = uop->vfp(uop->reg_src[1]);
    if (uop->is_reverse_divide()) {
        std::swap(adenominator, adenominator);
    }

    // copy to container that can extract any width
    const ampere::archx::vector_t vnumerator{{anumerator[0], anumerator[1]}};
    const ampere::archx::vector_t vdenominator{{adenominator[0], adenominator[1]}};

    archx::pred_reg_t pval = 0xFFFF;
    if (uop->is_sve()) {
        pval = uop->pred(uop->governing_predicate_reg());
        if (archx::pred_all_zero(pval, esize)) {
            return EarlyExitReason::FPDivAllPredicatedOff;
        }
    }

    // for vector ops, take the last reason as the reason if early exiting
    EarlyExitReason ee_reason{EarlyExitReason::Invalid};
    for (int e = 0; e < elements; e++) {
        if (pval == 0xFFFF || archx::get_pred_bit(pval, e, esize)) { // governing predicate bit == true
            const std::uint64_t numerator = vnumerator.get_vector_element_u64(e, esize);
            const std::uint64_t denominator = vdenominator.get_vector_element_u64(e, esize);
            ee_reason = test_fdiv_early_exit(numerator, denominator, esize);
            if (ee_reason == EarlyExitReason::Invalid) { // not early exit
                break;
            }
        }
    }
    return ee_reason;
}

EarlyExitReason IXU::test_idiv_early_exit_u64(std::uint64_t numerator, std::uint64_t denominator) const
{
    // integer divide early-exit cases:
    //   1) denominator is zero (result is 0)
    //   2) numerator is less than the denominator (result is 0)
    //   3) denominator is a power of 2 (simple shift)
    if (denominator == 0) {
        return EarlyExitReason::UDivDenominatorZero;
    }
    if (numerator < denominator) {
        return EarlyExitReason::UDivNumLessThanDenom;
    }
    if (util::is_power_of_two(denominator)) {
        return EarlyExitReason::UDivDenomPower2;
    }

    if (idiv_early_out_plus_minus_one) {
        if (numerator == denominator) { // answer is 1
            return EarlyExitReason::UDivNumEqualDenom;
        }
        // numerator > denominator
        std::uint64_t numerator_lrsb = div_supp::count_bits_u64(numerator);
        std::uint64_t denominator_lrsb = div_supp::count_bits_u64(denominator);
        if (numerator_lrsb == denominator_lrsb) {
            // answer is 1
            return EarlyExitReason::UDivLeadingRedundantSignBits;
        }
    }
    return EarlyExitReason::Invalid;
}

EarlyExitReason IXU::test_idiv_early_exit_i64(std::int64_t numerator, std::int64_t denominator) const
{
    // integer divide early-exit cases:
    //   1) denominator is zero (result is 0)
    //   2) numerator is less than the denominator (result is 0)
    //   3) denominator is a power of 2 (positive or negative)

    const std::uint64_t abs_numerator = std::abs(numerator);
    const std::uint64_t abs_denominator = std::abs(denominator);

    if (denominator == 0) {
        return EarlyExitReason::SDivDenominatorZero;
    }
    if (abs_numerator < abs_denominator) {
        return EarlyExitReason::SDivNumLessThanDenom;
    }
    if (util::is_power_of_two(abs_denominator)) {
        return EarlyExitReason::SDivDenomPower2;
    }

    if (idiv_early_out_plus_minus_one) {
        if (abs_numerator == abs_denominator) { // answer is 1,-1
            return EarlyExitReason::SDivNumEqualDenom;
        }
        // abs_numerator > abs_denominator
        std::uint64_t numerator_lrsb = div_supp::count_bits_u64(abs_numerator);
        std::uint64_t denominator_lrsb = div_supp::count_bits_u64(abs_denominator);
        if (numerator_lrsb == denominator_lrsb) {
            // answer is 1,-1
            return EarlyExitReason::SDivLeadingRedundantSignBits;
        }
    }
    return EarlyExitReason::Invalid;
}

static std::uint32_t idiv_max_iterations(std::uint32_t esize_bits, unsigned divide_radix)
{
    const std::uint32_t bits_per_iteration = fn_bits_per_iteration(divide_radix);
    // round up
    return (esize_bits + bits_per_iteration - 1) / bits_per_iteration;
}

unsigned IXU::idiv_latency(unsigned esize, unsigned srt_iterations) const
{
    // limit the variable iterations to avoid exceeding the max iterations
    const unsigned max_iterations = idiv_max_iterations(esize, divide_radix);
    const unsigned prem0 = 1;
    const unsigned fx00 = 1; // decoding
    const unsigned prescale = 1;
    // 'scale' is a new front-end cycle for radix64
    const std::uint32_t scale = (divide_radix == 64) ? 1 : 0;
    const unsigned postscale = 1;
    const unsigned round = 1; // not really rounding, sign-extension
    const unsigned front_end = fx00 + prescale + scale;
    const unsigned back_end = postscale + round;
    // The 2 extra cycles is because we share the back end pipeline with floating point
    const unsigned variable_back_end = 2 + back_end;
    const unsigned var_latency = front_end + srt_iterations + prem0 + variable_back_end;
    const unsigned max_latency = front_end + max_iterations + back_end;
    if (divide_fixed_latency) {
        DBG_DLAT("idiv_latency FIXED max {}, var {},  srt_iterations {}", max_latency, var_latency, srt_iterations);
        return max_latency;
    }
    DBG_DLAT("idiv_latency max {}, var {},  srt_iterations {}", max_latency, var_latency, srt_iterations);
    return std::min(max_latency, var_latency);
}

unsigned IXU::sdiv_iterations(std::int64_t numerator, std::int64_t denominator, unsigned esize) const
{
    unsigned iterations{0};
    if (numerator && denominator) { // zero tests
        if (esize == 32) {
            iterations = s32_div_iterations(numerator, denominator);
        } else {
            iterations = s64_div_iterations(numerator, denominator);
        }
    }
    return iterations;
}

unsigned IXU::udiv_iterations(std::uint64_t numerator, std::uint64_t denominator, unsigned esize) const
{
    unsigned iterations{0};
    if (numerator && denominator) { // zero tests
        if (esize == 32) {
            iterations = u32_div_iterations(numerator, denominator);
        } else {
            iterations = u64_div_iterations(numerator, denominator);
        }
    }
    return iterations;
}

std::uint64_t IXU::vector_idiv_iterations_config(const Uop& uop, const generic_vector_pair_t& nd) const // SVE only
{
    const bool is_unsigned = uop->is_vector_udiv();
    const int elements = uop->uop_info()->vector_element_count;
    const int esize = uop->uop_info()->vector_element_size;
    const archx::pred_reg_t pval = uop->pred(uop->governing_predicate_reg());
    std::vector<unsigned> element_iterations; // hold iterations for each element

    for (int elem = 0; elem < elements; elem++) {
        if (archx::get_pred_bit(pval, elem, esize)) { // governing predicate bit == true
            unsigned iterations{0};
            if (is_unsigned) {
                const std::uint64_t numerator = nd.numerator.u64[elem];
                const std::uint64_t denominator = nd.denominator.u64[elem];
                DBG_DIVEE2("vu64 numerator {:016x}  denominator {:016x}\n", numerator, denominator);
                iterations = udiv_iterations(numerator, denominator, esize);
            } else { // signed
                const std::int64_t numerator = nd.numerator.i64[elem];
                const std::int64_t denominator = nd.denominator.i64[elem];
                DBG_DIVEE2("vs64 numerator {:016x}  denominator {:016x}\n", numerator, denominator);
                iterations = sdiv_iterations(numerator, denominator, esize);
            }
            element_iterations.push_back(iterations);
        }
    } // for
    // find worst case iterations for vector.

    // only empty if all relevant governing predicate bits are 0. And
    // that is handled in the early exit config
    AMPERE_ASSERT(element_iterations.size() > 0, "Should not be empty");
    auto max_iterations = std::max_element(element_iterations.begin(), element_iterations.end());
    const unsigned latency = idiv_latency(esize, *max_iterations);
    DBG_DLAT("sve sdiv txid: {} inum: {} iterations: {} latency: {}",
             uop->get_txid(),
             uop->inum().id(),
             *max_iterations,
             latency);
    return latency;
}

std::uint64_t IXU::scalar_idiv_iterations_config(const Uop& uop, const generic_vector_pair_t& nd) const
{
    unsigned iterations{0};
    const int esize = uop->uop_info()->vector_element_size;
    if (uop->is_udiv()) { // scalar unsigned integer divide
        std::uint64_t numerator = nd.numerator.u64[0];
        std::uint64_t denominator = nd.denominator.u64[0];
        DBG_DIVEE2("scalar u64 numerator {:016x}  denominator {:016x}\n", numerator, denominator);
        iterations = udiv_iterations(numerator, denominator, esize);
    } else { // scalar signed integer divide
        std::int64_t numerator = nd.numerator.i64[0];
        std::int64_t denominator = nd.denominator.i64[0];
        DBG_DIVEE2("scalar s64 numerator {:016x}  denominator {:016x}\n", numerator, denominator);
        iterations = sdiv_iterations(numerator, denominator, esize);
    }
    const unsigned latency = idiv_latency(esize, iterations);
    DBG_DLAT("scalar div txid: {} inum: {} iterations: {} latency: {}",
             uop->get_txid(),
             uop->inum().id(),
             iterations,
             latency);
    return latency;
}

EarlyExitReason IXU::idiv_vector_early_exit_config(const Uop& uop, const generic_vector_pair_t& nd, int elements) const
{
    // used for scalars too with elements == 1.
    const bool is_unsigned = uop->is_udiv();
    const int esize = uop->uop_info()->vector_element_size;
    archx::pred_reg_t pval = 0xFFFF; // for nonSVE and scalars
    if (uop->is_sve()) {
        pval = uop->pred(uop->governing_predicate_reg());
        if (archx::pred_all_zero(pval, esize)) {
            return is_unsigned ? EarlyExitReason::UDivAllPredicatedOff : EarlyExitReason::SDivAllPredicatedOff;
        }
    }

    // we take the last reason as the early exit reasons for vector ops
    EarlyExitReason ee_reason = EarlyExitReason::Invalid;
    for (int elem = 0; elem < elements; elem++) {
        if (pval == 0xFFFF || archx::get_pred_bit(pval, elem, esize)) { // governing predicate bit == true
            if (is_unsigned) {                                          // unsigned integer divide
                const std::uint64_t numerator = nd.numerator.u64[elem];
                const std::uint64_t denominator = nd.denominator.u64[elem];
                ee_reason = test_idiv_early_exit_u64(numerator, denominator);
                if (ee_reason == EarlyExitReason::Invalid) { // not early exit
                    break;
                }
            } else { // signed integer divide
                const std::int64_t numerator = nd.numerator.i64[elem];
                const std::int64_t denominator = nd.denominator.i64[elem];
                ee_reason = test_idiv_early_exit_i64(numerator, denominator);
                if (ee_reason == EarlyExitReason::Invalid) { // not early exit
                    break;
                }
            }
        }
    }

#if defined(DEBUG_DIV_EARLY_EXIT)
    std::ostringstream os;
    os << ee_reason;
    DBG_DIVEE("EARLY EXIT = {}", os.str());
#endif
    return ee_reason;
}

} // namespace ampere::core
