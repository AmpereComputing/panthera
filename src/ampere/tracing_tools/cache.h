//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_TRACING_CACHE_H
#define AMPERE_TRACING_CACHE_H

#include "ampere/archx/addr.h"
#include "ampere/core/hash.h"
#include "ampere/memory_stream/memory_stream.h"
#include "ampere/util/bits.h"
#include "ampere/uarch_common/chip_name.h"

using namespace ampere::core;
using namespace ampere::archx;
using namespace ampere::uarch_common;

namespace ampere::tracer {

uint64_t log2(uint64_t num)
{
    if (num == 0)
        throw std::runtime_error("Can't take log2 of 0");
    if (!util::is_power_of_two(num))
        throw std::runtime_error("Number is not a power of 2");
    return util::log2(num);
}

struct CacheContents
{
    uint64_t age;
    bool code;
    bool modified;
};

template<typename Addr>
class Cache
{
public:
    using value_type = CacheContents;
    using Set = std::map<uint64_t, CacheContents>;

    Cache(uint64_t size_bytes, uint64_t num_ways, ChipName chip)
        : size_bytes{size_bytes}, num_ways{num_ways}, num_sets{size_bytes / (1 << line_bits) / num_ways}, chip{chip}
    {
        set_bits = log2(num_sets);
        sets.resize(num_sets);
    }

    Cache(uint64_t size_bytes) : Cache(size_bytes, 4 /*num_ways*/, ChipName::rogue) {}

    virtual ~Cache() = default;

    bool lookup(Addr address, bool write)
    {
        auto index = this->calculate_index(address);
        if (index >= num_sets)
            throw std::logic_error("set index is larger than the number of sets");
        auto tag = this->calculate_tag(address);
        auto& set = sets[index];
        auto it = set.find(tag);

        if (it != set.end()) {
            it->second.age = current_age++;
            it->second.modified = it->second.modified || write;
            return true;
        }
        return false;
    }

    void refill(Addr address, bool code, bool write)
    {
        auto index = this->calculate_index(address);
        if (index >= num_sets)
            throw std::logic_error("set index is larger than the number of sets");
        auto tag = this->calculate_tag(address);
        auto& set = sets[index];
        auto it = set.find(tag);

        if (it != set.end()) {
            it->second.age = current_age++;
            it->second.code = code;
            it->second.modified = write;
            return;
        }

        if (set.size() == num_ways) {
            set.erase(find_victim(set));
        }
        set.insert_or_assign(tag, CacheContents{current_age++, code});
    }

    bool append_to_memory_stream(ms::MemoryStreamWriter& mem_stream)
    {
        struct MSContents
        {
            uint64_t pa;
            bool code;
            bool modified;
        };
        std::multimap<uint64_t, MSContents> l2c_contents;

        // Insert each content into a multimap based on age to sort as we go
        for (uint64_t index = 0; index < sets.size(); index++) {
            auto& set = sets[index];
            for (auto& [tag, contents] : set) {
                uint64_t pa = this->reconstruct_pa(tag, index);
                l2c_contents.emplace(contents.age, MSContents{pa, contents.code, contents.modified});
            }
        }

        // Write out cache lines in order from LRU to MRU
        for (auto& [age, contents] : l2c_contents) {
            ms::Access ms_access;

            ms_access.set_type(contents.modified ? ms::Access_Type_WRITE : ms::Access_Type_READ);
            ms_access.set_source(contents.code ? ms::Access_Source_INSTRUCTION : ms::Access_Source_DATA);
            ms_access.set_pa(contents.pa);
            ms_access.set_size(1ul << line_bits);
            ms_access.set_offset(age);

            if (!mem_stream.write(ms_access))
                return false;

            ms_access.Clear();
        }
        return true;
    }

    bool to_memory_stream(boost::filesystem::path filepath)
    {
        ms::MemoryStreamWriter memstream(filepath, size_bytes, size_bytes, true /*unified cache*/);
        return append_to_memory_stream(memstream);
    }

protected:
    virtual uint64_t calculate_index(Addr address)
    {
        return set_bits == 0 ? 0 : address.extract_range(line_bits + set_bits - 1, line_bits);
    }

    virtual uint64_t calculate_tag(Addr address) { return address.extract_range(63, line_bits + set_bits); }

    virtual uint64_t reconstruct_pa(uint64_t tag, uint64_t index) { return ((tag << set_bits) | index) << line_bits; }

    Set::const_iterator find_victim(const Set& set) const
    {
        auto victim = set.begin();
        for (auto it = set.begin(); it != set.end(); ++it) {
            if (it->second.age < victim->second.age) {
                victim = it;
            }
        }
        return victim;
    }

    uint64_t size_bytes;
    uint64_t line_bits{6}; // 64 byte cache line
    uint64_t num_ways;
    uint64_t num_sets;
    uint64_t set_bits;
    ChipName chip;

    uint64_t current_age{0};

    std::vector<Set> sets;
};

template<typename Addr>
class L2Cache : public Cache<Addr>
{
public:
    L2Cache(uint64_t size_bytes, uint64_t num_ways, ChipName chip)
        : Cache<Addr>(size_bytes, num_ways, chip), bank_hash(6, 11)
    {
        if (this->set_bits != 12) {
            // Here, set_bits counts both bank and set bits
            throw std::logic_error("L2Cache::calculate_index expects 9 set bits and 3 bank bits in the L2");
        }
        if (this->line_bits != 6) {
            throw std::logic_error("L2Cache::calculate_index expects 6 line bits in the L2");
        }
    }

    virtual ~L2Cache() = default;

protected:
    virtual uint64_t calculate_index(Addr address)
    {
        switch (this->chip) {
        case ChipName::siryn:
        case ChipName::banshee:
        case ChipName::rogue: {
            uint64_t index_low = address.extract_range(11, 9);
            uint64_t index_high = address.extract_range(23, 18) ^ address.extract_range(17, 12);
            uint64_t set_index = (index_high << 3) | index_low;
            uint64_t bank = bank_hash(address.extract_range(11, 6));
            if ((bank << (this->set_bits - bank_bits)) & set_index) {
                throw std::out_of_range("bank and set bits collide");
            }
            return (bank << (this->set_bits - bank_bits)) | set_index;
        }
        case ChipName::domino: {
            uint64_t low_half_lsb = bank_bits + 6;
            uint64_t low_half_msb = low_half_lsb + (this->set_bits - bank_bits - 1);
            uint64_t high_half_lsb = low_half_msb + 1;
            uint64_t high_half_msb = high_half_lsb + (this->set_bits - bank_bits - 1);
            uint64_t set_index =
                address.extract_range(low_half_msb, low_half_lsb) ^ address.extract_range(high_half_msb, high_half_lsb);
            uint64_t bank = address.extract_range(8, 6) ^ address.extract_range(11, 9) ^ address.extract_range(14, 12);
            if ((bank << (this->set_bits - bank_bits)) & set_index) {
                throw std::out_of_range("bank and set bits collide");
            }
            return (bank << (this->set_bits - bank_bits)) | set_index;
        }
        default:
            throw std::runtime_error("Unexpected chip encountered");
        }
        return 0; // not-reached (compiler does not detect this)
    }

    virtual uint64_t calculate_tag(Addr address) { return address.extract_range(63, this->line_bits); }

    virtual uint64_t reconstruct_pa(uint64_t tag, uint64_t index) { return tag << this->line_bits; }

private:
    uint64_t bank_bits{3};
    RauHash bank_hash;
};

} // namespace ampere::tracer

#endif // AMPERE_TRACING_CACHE_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
