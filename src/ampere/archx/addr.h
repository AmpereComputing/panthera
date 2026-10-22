//
// Copyright (c) 2018-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <limits>

#include "ampere/archx/bitfield.h"
#include "ampere/archx/page_size.h"

namespace ampere::archx {

using json = nlohmann::json;

template<typename T>
struct CommonAddrDef
{
    inline static constexpr unsigned CHUNK_SIZE = 8;
    inline static constexpr unsigned CHUNK_BITWIDTH = 3;
    inline static constexpr unsigned LINE_SIZE = 64;
    inline static constexpr unsigned LINE_BITWIDTH = 6;
};
struct VirtAddrDef : CommonAddrDef<VirtAddrDef>
{};
struct PhysAddrDef : CommonAddrDef<PhysAddrDef>
{};
struct ProgramCounterDef : CommonAddrDef<ProgramCounterDef>
{};

template<typename T>
class Addr : public Bitfield
{
public:
    inline static constexpr unsigned CHUNK_SIZE = T::CHUNK_SIZE;
    inline static constexpr unsigned CHUNK_BITWIDTH = T::CHUNK_BITWIDTH;
    inline static constexpr unsigned LINE_SIZE = T::LINE_SIZE;
    inline static constexpr unsigned LINE_BITWIDTH = T::LINE_BITWIDTH;
    inline static constexpr std::uint64_t LINE_MASK = (1ULL << LINE_BITWIDTH) - 1ULL;
    inline static constexpr std::uint64_t HALF_LINE_MASK = (1ULL << (LINE_BITWIDTH - 1ULL)) - 1ULL;

    Addr() = default;
    explicit Addr(std::uint64_t addr) : Bitfield(addr) {}
    explicit Addr(const Bitfield& other) : Bitfield(other) {}

    inline static Addr from_line_number(uint64_t line_number) { return Addr{line_number << LINE_BITWIDTH}; }

    Addr& operator+=(std::uint64_t rhs)
    {
        this->contents += rhs;
        return *this;
    }

    Addr& operator+=(PageSize rhs)
    {
        this->contents += (1ULL << rhs.page_bits());
        return *this;
    }

    friend Addr operator+(Addr lhs, std::uint64_t rhs)
    {
        lhs += rhs;
        return lhs;
    }

    friend Addr operator+(Addr lhs, PageSize rhs)
    {
        lhs += (1ULL << rhs.page_bits());
        return lhs;
    }

    friend uint64_t operator>>(Addr lhs, unsigned int rhs) { return lhs.value() >> rhs; }

    friend uint64_t operator<<(Addr lhs, unsigned int rhs) { return lhs.value() << rhs; }

    Addr& operator-=(std::uint64_t rhs)
    {
        this->contents -= rhs;
        return *this;
    }

    Addr& operator-=(PageSize rhs)
    {
        this->contents -= (1ULL << rhs.page_bits());
        return *this;
    }

    friend std::int64_t operator-(Addr lhs, const Addr& rhs) { return lhs.value() - rhs.value(); }

    friend Addr operator-(Addr lhs, std::uint64_t rhs) { return Addr{lhs.value() - rhs}; }

    friend Addr operator-(Addr lhs, PageSize rhs) { return Addr{lhs.value() - (1UL << rhs.page_bits())}; }

    friend Addr operator&(Addr lhs, const Addr& rhs) { return Addr{Bitfield(lhs) & Bitfield(rhs)}; }

    friend Addr operator&(Addr lhs, std::uint64_t rhs) { return Addr{Bitfield(lhs) & rhs}; }

    friend Addr operator|(Addr lhs, const Addr& rhs) { return Addr{Bitfield(lhs) | Bitfield(rhs)}; }

    friend Addr operator|(Addr lhs, std::uint64_t rhs) { return Addr{Bitfield(lhs) | rhs}; }

    friend bool operator<(const Addr& lhs, const Addr& rhs) { return lhs.value() < rhs.value(); }
    friend bool operator>(const Addr& lhs, const Addr& rhs) { return rhs < lhs; }
    friend bool operator<=(const Addr& lhs, const Addr& rhs) { return !(lhs > rhs); }
    friend bool operator>=(const Addr& lhs, const Addr& rhs) { return !(lhs < rhs); }

    bool same_upper_bits(const Addr& other, int ignore_bits) const
    {
        return this->extract_upper_range(ignore_bits) == other.extract_upper_range(ignore_bits);
    }

    bool same_cache_line(const Addr& other) const { return same_upper_bits(other, LINE_BITWIDTH); }

    bool same_half_cache_line(const Addr& other) const { return same_upper_bits(other, LINE_BITWIDTH - 1); }

    bool same_page(const Addr& other, const PageSize& ps) const { return same_upper_bits(other, ps.page_bits()); }
    bool same_page_4k(const Addr& other) const { return same_page(other, PageSize::Size::PS_4KB); }
    bool same_page_64k(const Addr& other) const { return same_page(other, PageSize::Size::PS_64KB); }

    // This operates on cacheline addresses -- full addresses shifted right by 6 bits.
    // It returns whether cacheline 'other' is in the same 'ps'-sized page as this cacheline.
    bool cache_lines_in_same_page(const Addr& other, const PageSize& ps) const
    {
        return same_upper_bits(other, ps.page_bits() - LINE_BITWIDTH);
    }

    // Return the address with some lower bits bits zeroed.
    Addr masked_address(uint64_t granularity) const { return Addr(this->contents & (~(granularity - 1ULL))); }

    // Return the address with some lower bits bits set to 1.
    Addr unmasked_address(uint64_t granularity) const { return Addr(this->contents | (granularity - 1ULL)); }

    // Return the address with the line bits zeroed.
    // Example: Addr 0x13a179ff8 returns Addr 0x13a179fc0
    Addr line_address() const { return Addr(this->contents & (~LINE_MASK)); }

    // Return the address with the half-line bits zeroed.
    // Example: Addr 0x13a179ff8 returns Addr 0x13a179fe0
    Addr half_line_address() const { return Addr(this->contents & (~HALF_LINE_MASK)); }

    // Return the address corresponding to the beginning of the *next* cache
    // line. Example: Addr 0x13a179ff8 returns Addr 0x13a17a000
    Addr next_line_address() const { return Addr((this->contents & (~LINE_MASK)) + LINE_SIZE); }

    // Return the address without the line bits.
    // Example: Addr 0x13a179ff8 returns 0x4e85e7f
    uint64_t line_number() const { return this->extract_upper_range(LINE_BITWIDTH); }

    // Return the address with the bits above the cacheline masked off
    uint64_t line_offset(int line_bits = LINE_BITWIDTH) const { return this->extract_range(line_bits - 1, 0); }

    // Return the address with the page bits zeroed.
    // Example: Addr 0x13a179ff8 returns Addr 0x13a179000 for a 4kb page
    Addr page_address(const PageSize& ps) const
    {
        return Addr(this->contents & (~((uint64_t)0x1) << (ps.page_bits() - 1)));
    }
    Addr page_address_4k() const { return page_address(PageSize::Size::PS_4KB); }

    // Return the address without the page bits.
    // Example: Addr 0x13a179ff8 returns 0x13a179 for a 4kb page
    uint64_t page_number(const PageSize& ps) const { return this->extract_upper_range(ps.page_bits()); }
    uint64_t page_number_4k() const { return page_number(PageSize::Size::PS_4KB); }

    // Return only the page bits which are not also line bits from the address.
    // Example: Addr 0x13a179ff8 returns 0x3f for a 4kb page
    uint64_t page_offset(const PageSize& ps) const { return this->extract_range(ps.page_bits() - 1, 6); }
    uint64_t page_offset_4k() const { return page_offset(PageSize::Size::PS_4KB); }

    // Given the access size, return a bitvector of the byte-pairs in a line which are affected by a load/store
    uint64_t line_bytepair_mask(unsigned size) const
    {
        uint64_t bytemask = line_bytemask(size);
        uint64_t odd_bits = bytemask & 0x5555555555555555ULL;
        uint64_t odd_pairs = odd_bits | (odd_bits << 1);
        uint64_t even_bits = bytemask & 0xaaaaaaaaaaaaaaaaULL;
        uint64_t even_pairs = even_bits | (even_bits >> 1);

        return odd_pairs | even_pairs;
    }

    // Given the access size, return a bitvector of the bytes in a line which are affected by a load/store
    uint64_t line_bytemask(unsigned size) const
    {
        uint64_t size_mask = (size < LINE_SIZE) ? (1UL << size) - 1 : std::numeric_limits<uint64_t>::max();
        return size_mask << this->line_offset();
    }

    // Return an 8-bit vector of which 8-byte chunks within a cache line are touched by an access
    uint64_t chunk_bitmap(unsigned size) const
    {
        uint64_t bitmap{0};
        Addr addr{*this};

        // Mark the chunk of every 8th byte
        for (unsigned i = 0; i < size; i += CHUNK_SIZE) {
            bitmap |= (1ULL << addr.extract_range(LINE_BITWIDTH - 1, CHUNK_BITWIDTH));
            addr += CHUNK_SIZE;
        }

        // And then do the last byte of the access in case that extends into one more chunk
        addr = *this + size - 1;
        bitmap |= (1ULL << addr.extract_range(LINE_BITWIDTH - 1, CHUNK_BITWIDTH));

        return bitmap;
    }
};

template<typename T>
class AddrRange
{
public:
    T addr{};
    uint64_t size{};

    AddrRange() = default;
    AddrRange(T addr, uint64_t size) : addr(addr), size(size) {}
    AddrRange(T start, T end) : addr(start), size(end - start) {}

    bool operator==(const AddrRange<T>& other) { return addr == other.addr && size == other.size; }
    bool operator!=(const AddrRange<T>& other) { return !operator==(other); }

    bool empty() const { return !size; }
    T last_byte_addr() const { return end_addr() - 1; }

    bool line_split() const { return !addr.same_cache_line(last_byte_addr()); }
    auto line_bytemask() const { return addr.line_bytemask(size); }
    auto line_bytepair_mask() const { return addr.line_bytepair_mask(size); }
    auto chunk_bitmap() const { return addr.chunk_bitmap(size); }

    bool covers(const AddrRange<T>& other) const
    {
        if (empty() || other.empty())
            return false;

        auto end = end_addr();
        auto other_end = other.end_addr();
        return other.addr >= addr && other.addr < end && other_end <= end;
    }

    bool intersects(const AddrRange<T>& other, uint64_t granularity = 1) const
    {
        if (empty() || other.empty())
            return false;

        auto lo = addr.masked_address(granularity);
        auto other_lo = other.addr.masked_address(granularity);
        auto hi = last_byte_addr().unmasked_address(granularity);
        auto other_hi = other.last_byte_addr().unmasked_address(granularity);

        return (other_lo >= lo && other_lo <= hi) || (lo >= other_lo && lo <= other_hi);
    }

    void expand(const AddrRange<T>& other)
    {
        auto end = std::max(end_addr(), other.end_addr());
        addr = std::min(addr, other.addr);
        size = end - addr;
    }

private:
    T end_addr() const { return addr + size; }
};

template<typename T>
bool address_range_a_covers_b(T a, uint64_t a_size, T b, uint64_t b_size)
{
    AddrRange<T> a_range{a, a_size};
    AddrRange<T> b_range{b, b_size};
    return a_range.covers(b_range);
}

template<typename T>
bool address_range_a_intersects_b(T a, uint64_t a_size, T b, uint64_t b_size, uint64_t granularity)
{
    AddrRange<T> a_range{a, a_size};
    AddrRange<T> b_range{b, b_size};
    return a_range.intersects(b_range, granularity);
}

using VirtAddr = Addr<VirtAddrDef>;
using PhysAddr = Addr<PhysAddrDef>;
using ProgramCounter = Addr<ProgramCounterDef>;

inline VirtAddr pc2va(const ProgramCounter& pc)
{
    return VirtAddr(pc.value());
}

inline VirtAddr operator""_va(unsigned long long x)
{
    return VirtAddr{x};
}
inline PhysAddr operator""_pa(unsigned long long x)
{
    return PhysAddr{x};
}
inline ProgramCounter operator""_pc(unsigned long long x)
{
    return ProgramCounter{x};
}

// Allow conversion to/from JSON as hexadecimal strings using ADL
inline void bitfield_to_json(json& j, const Bitfield& bitfield)
{
    j = fmt::format("0x{:x}", bitfield.value());
}

template<typename T>
inline void to_json(json& j, const Addr<T>& address)
{
    bitfield_to_json(j, address);
}

inline Bitfield json_to_bitfield(const json& j)
{
    if (j.is_string())
        return Bitfield(std::stoull(j.get<std::string>(), nullptr, 0));
    else
        return Bitfield(j.get<uint64_t>());
}

template<typename T>
inline void from_json(const json& j, Addr<T>& address)
{
    address = Addr<T>(json_to_bitfield(j));
}

} // namespace ampere::archx

template<typename T>
struct fmt::formatter<ampere::archx::Addr<T>> : fmt::ostream_formatter
{};

namespace std {
template<typename T>
struct hash<ampere::archx::Addr<T>>
{
    size_t operator()(const ampere::archx::Addr<T>& addr) const { return hash<uint64_t>()(addr.value()); }
};
} // namespace std

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
