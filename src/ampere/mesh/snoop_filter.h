//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <utility>

#include "ampere/mesh/chi/snoop.h"
#include "ampere/mesh/mesh_device.h"
#include "ampere/sc/sc.h"
#include "ampere/uarch_common/cache.h"
#include "ampere/util/enum.h"

make_ns_enum(ampere::mesh, RsField,
    NONE,
    IN_USE,
    VICTIM
);

namespace ampere::mesh {

using namespace chi;
using namespace sc;
using namespace uarch_common;

enum StVecUpdateOpcode {
    STVEC_NONE,
    STVEC_SET_ONLY,
    STVEC_CLEAR_ONLY,
    STVEC_SET_AND_CLEAR,
    STVEC_CLEAR_ALL,
    STVEC_CLEAR_ALL_EXCEPT_RN
};

enum PstUpdateOpcode {
    PST_NOUPDATE,
    PST_IS_NODEID,
    PST_CALC_NEW
};

class SnoopFilter : public CacheConfiguration
{
public:
    SnoopFilter(const sc_core::sc_module_name& name);

    Param<int> bank_bits{
        "bank_bits",
        "Number of bits to address a bank (used for address decoding; NOT part of the size calculation!)",
        2};
    Param<int> low_tag_bits{"low_tag_bits", "Bits between the bank and the start of the set", 4};

    std::tuple<Index, int> lookup(Txn transaction, PhysAddr address, bool trace);
    int find_empty_way(Txn transaction, Index index);
    int find_victim(Txn transaction, Index index);

    void fill(Txn transaction, Index set, int way, PhysAddr address, SnoopStatus& status, bool trace);
    // void reset(Index set, int way, PhysAddr address);
    void invalidate(Index set, int way);

    void set_rs(Txn transaction, Index set, int way, RsField rs)
    {
        ev_set_rs(transaction, evd_set(set), evd_way(way), evd_rs(rs));
        array.at(set).at(way).rs = rs;
    }

    void lock(Index set, int way) { array.at(set).at(way).rs = RsField::IN_USE; }
    void unlock(Index set, int way) { array.at(set).at(way).rs = RsField::NONE; }

    RsField get_rs(Index set, int way) { return array.at(set).at(way).rs; }
    void update_lru(Index set, int way) { array.at(set).at(way).age = ++current_age; }

    void preload(NodeId stash_node, PhysAddr address, FinalState state, NodeIdToStasher node_id_to_stasher);

    PhysAddr get_address(Index set, int way) { return array.at(set).at(way).address; }
    const SnoopStatus& get_data(Index set, int way) { return array.at(set).at(way).status; }
    bool get_trace(Index set, int way) const { return array.at(set).at(way).trace; }

    int sf_occupancy() const;
    void dump(std::ostream& os = std::cout) const override;

#ifdef USE_BOOST_SERIALIZATION
    void serialize(boost::archive::text_oarchive& oa) const override;
    void deserialize(boost::archive::text_iarchive& ia) override;
    void serialize_sf(boost::archive::text_oarchive& oa) const;
    void deserialize_sf(boost::archive::text_iarchive& ia);
#endif

private:
    Event ev_access{"access", "Cache access"};
    Event ev_refill{"refill", "Cache refill"};
    Event ev_eviction{"eviction", "Cache eviction"};
    Event ev_allocate{"allocate", "Cache alloc"};
    Event ev_set_rs{"set_rs", "Set RS field of SF Entry"};

    EventData<bool> evd_hit{"hit", "Cache hit"};
    EventData<int> evd_way{"way", "Cache way"};
    EventData<int> evd_set{"set", "Cache set"};
    EventData<int> evd_bank{"bank", "Cache bank"};
    EventData<RsField> evd_rs{"rs", "RS field encoding"};
    EventData<int> evd_num_sharers{"num_sharers", "number of sharers in the entry"};
    EventData<int> evd_state{"state", "state of the snoop cache entry"};

    struct Entry
    {
        PhysAddr address;
        SnoopStatus status;
        std::uint64_t tag{0};
        std::uint64_t age{0};
        RsField rs{RsField::NONE};

        bool precise_tracking{true};
        bool proxy_owned{false};
        bool trace{false}; // for debug

        bool valid() const { return status.get_state() != FinalState::I; }

#ifdef USE_BOOST_SERIALIZATION
        template<typename Archive>
        void serialize(Archive& ar, const unsigned int version)
        {
            ar & address;
            ar & status;
            ar & tag;
            ar & age;
            ar & rs;
            ar & precise_tracking;
            ar & proxy_owned;
        }
#endif
    };

    std::vector<std::vector<Entry>> array;
    std::uint64_t current_age{0};

    std::pair<Index, int> compute_set(PhysAddr address);
    std::uint64_t compute_tag(PhysAddr address);
};

} // namespace ampere::mesh
