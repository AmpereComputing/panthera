//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/mesh/snoop_filter.h"

#include <utility>

#define TRACE(entry, msg, ...)                                                       \
    if ((entry).trace) {                                                             \
        AMPERE_INFO("[{}] {}: " msg, sc_core::sc_time_stamp(), name(), __VA_ARGS__); \
    }

namespace ampere::mesh {

using util::mask;

SnoopFilter::SnoopFilter(const sc_core::sc_module_name& name) : CacheConfiguration{name}
{
    array.resize(num_sets);

    for (auto& set : array)
        set.resize(num_ways);
}

std::tuple<Index, int> SnoopFilter::lookup(Txn transaction, PhysAddr address, bool trace)
{
    auto [index, bank] = compute_set(address);
    auto tag = compute_tag(address);
    auto& set = array[index];

    for (int way = 0; way < (int)num_ways; ++way) {
        if (set[way].valid() && set[way].tag == tag) {
            ev_access(transaction, evd_hit(true), evd_way(way), evd_set(index >> bank_bits), evd_bank(bank), address);
            set[way].trace = trace;
            return {index, way};
        }
    }

    ev_access(transaction, evd_hit(false), evd_set(index >> bank_bits), evd_bank(bank), address);
    return {index, -1};
}

int SnoopFilter::find_empty_way(Txn transaction, Index index)
{
    auto& set = array[index];

    for (int way = 0; way < (int)num_ways; ++way) {
        if (!set[way].valid() && set[way].rs == RsField::NONE) {
            if (transaction)
                ev_allocate(transaction, evd_set(index), evd_way(way), set[way].address);
            return way;
        }
    }

    return -1;
}

int SnoopFilter::find_victim(Txn transaction, Index index)
{
    auto& set = array[index];
    int victim_way = -1;
    std::uint64_t victim_age = std::numeric_limits<std::uint64_t>::max();
    SnoopStatus victim_status;

    for (int way = 0; way < (int)num_ways; ++way) {
        auto& entry = set[way];

        if (entry.age < victim_age && entry.rs == RsField::NONE) {
            victim_age = entry.age;
            victim_way = way;
            victim_status = entry.status;
        }
    }

    if (victim_way != -1 && transaction)
        ev_eviction(transaction,
                    evd_set(index),
                    evd_way(victim_way),
                    evd_num_sharers(victim_status.count()),
                    set[victim_way].address);

    if (victim_way != -1)
        TRACE(set[victim_way], "SF entry set={} way={} selected as a victim", index, victim_way);

    return victim_way;
}

void SnoopFilter::fill(Txn transaction, Index set, int way, PhysAddr address, SnoopStatus& status, bool trace)
{
    auto tag = compute_tag(address);
    auto& entry = array.at(set).at(way);
    TRACE(entry,
          "filling SF set={} way={} old info (address={} status={}) -> new info (address {} status={})",
          set,
          way,
          entry.address,
          entry.status,
          address,
          status);
    AMPERE_ASSERT((entry.address.value() >> 6) == (address.value() >> 6) || entry.status.get_state() == FinalState::I,
                  "writing over a non-invalid entry (old address={}, new address={}) in the SF",
                  entry.address,
                  address);
    entry.address = address;
    entry.status = status;
    entry.tag = tag;
    entry.trace = trace;
    ev_refill(transaction,
              evd_set(set),
              evd_way(way),
              evd_num_sharers(status.count()),
              evd_state(static_cast<int>(status.get_state())),
              address);
}

void SnoopFilter::invalidate(Index set, int way)
{
    auto& entry = array.at(set).at(way);
    entry.status.reset();
}

/*
void SnoopFilter::reset(Index set, int way, PhysAddr address)
{
    auto tag = compute_tag(address);
    auto& entry = array.at(set).at(way);
    AMPERE_ASSERT(entry.address == address || entry.status.get_state() == FinalState::I, "resetting a non-invalid entry
in the SF"); entry.address = address; entry.status.reset(); entry.tag = tag;
}
*/

void SnoopFilter::preload(NodeId stash_node, PhysAddr address, FinalState state, NodeIdToStasher node_id_to_stasher)
{
    auto [index, bank] = compute_set(address);
    auto tag = compute_tag(address);
    auto& set = array[index];

    for (int way = 0; way < (int)num_ways; ++way) {
        if (set[way].valid() && set[way].tag == tag) {
            set[way].status.set_state(stash_node, state, true);
            return;
        }
    }

    int way = find_empty_way(nullptr, index);

    if (way == -1) {
        way = find_victim(nullptr, index);
        AMPERE_ASSERT(way != -1, "SF could not find victim way during preload; ways locked??");
        auto& entry = set.at(way);

        for (auto& node_id : entry.status.core_valid_as_set()) {
            auto stasher = node_id_to_stasher(node_id);
            // NOTE: The node_id below isn't actually used by a core/injector, so we can get
            // away with passing anything. But should we try harder to pass in the NodeId of
            // this PHA in case something changes?
            stasher->stash_invalidate(entry.address, node_id);
        }
    }

    auto& entry = set.at(way);
    entry.address = address;
    entry.tag = tag;
    entry.status.set_state(stash_node, state, true);
}

std::pair<Index, int> SnoopFilter::compute_set(PhysAddr address)
{
    if (this->set_bits == 0)
        return {0, 0};

    // We don't actually model a banked SF, so where you see "set_bits" here, it
    // referes to the sum of the actual set bits and the bank bits. And when we
    // compute the index, we need to incorporate the actual set with the bank.
    //
    uint64_t addr = address.value() >> 6;
    unsigned int bank = addr & mask(bank_bits);
    addr >>= (bank_bits + low_tag_bits);
    unsigned int set = addr & mask(set_bits - bank_bits);

    auto index = (set << bank_bits) + bank;

    if (index >= num_sets)
        throw std::logic_error("set index is larger than the number of sets");

    return {index, bank};
}

std::uint64_t SnoopFilter::compute_tag(PhysAddr address)
{
    uint64_t addr = address.value() >> 6;
    addr >>= bank_bits;
    unsigned int low_tag = addr & mask(low_tag_bits);
    addr >>= (low_tag_bits + set_bits - bank_bits);
    uint64_t tag = (addr << low_tag_bits) + low_tag;
    return tag;
}

int SnoopFilter::sf_occupancy() const
{
    int count = 0;
    for (auto set = 0u; set < array.size(); ++set) {
        for (auto way = 0u; way < array[set].size(); ++way) {
            auto& entry = array[set][way];
            if (entry.valid())
                count++;
        }
    }
    return count;
}

void SnoopFilter::dump(std::ostream& os) const
{
    fmt::print(os, "{}:\n\n", name());
    fmt::print(os, " Set | Way | L |       Addr         | State |   PST   | CVs\n");

    for (auto set = 0u; set < array.size(); ++set) {
        for (auto way = 0u; way < array[set].size(); ++way) {
            auto& entry = array[set][way];
            if (!entry.valid())
                continue;

            std::string pst;
            if (entry.status.primary_snoop_target().has_value())
                pst = fmt::format("{}", *entry.status.primary_snoop_target());
            else
                pst = "NA";
            fmt::print(os,
                       " {:3d} | {:2d}  | {} | 0x{:016x} |  {:2}   |  {:3}  |",
                       set,
                       way,
                       std::to_underlying(entry.rs) ? "X" : " ",
                       entry.address.value(),
                       entry.status.get_state(),
                       pst);
            for (auto cv : entry.status.core_valid_as_set())
                fmt::print(os, " {}", cv);
            os << "\n";
        }
    }
    os << this->name() << ": snoop filter occupancy: " << this->sf_occupancy();
    os << "\n";
}

#ifdef USE_BOOST_SERIALIZATION
void SnoopFilter::serialize_sf(boost::archive::text_oarchive& ar) const
{
    size_t entries{0};

    for (size_t index = 0; index < array.size(); index++) {
        for (size_t way = 0; way < array[index].size(); ++way) {
            const auto& entry = array[index][way];

            if (entry.status.primary_snoop_target() || entry.status.count())
                ++entries;
        }
    }

    ar & entries;

    for (size_t index = 0; index < array.size(); index++) {
        for (size_t way = 0; way < array[index].size(); ++way) {
            const auto& entry = array[index][way];

            if (entry.status.primary_snoop_target() || entry.status.count()) {
                ar & index;
                ar & way;
                ar & entry;
            }
        }
    }
}

void SnoopFilter::deserialize_sf(boost::archive::text_iarchive& ar)
{
    size_t entries;

    ar & entries;

    while (entries--) {
        size_t index;
        size_t way;

        ar & index;
        ar & way;

        auto& entry = array[index][way];

        ar & entry;
    }
}

void SnoopFilter::serialize(boost::archive::text_oarchive& oa) const
{
    oa << std::string{name()};
    serialize_sf(oa);
}

void SnoopFilter::deserialize(boost::archive::text_iarchive& ia)
{
    deserialize_sf(ia);
}
#endif // USE_BOOST_SERIALIZATION

} // namespace ampere::mesh
