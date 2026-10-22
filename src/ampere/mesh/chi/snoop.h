//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <fmt/core.h>
#include <fmt/ostream.h>
#include <limits>
#include <optional>
#include <set>
#include <variant>

#include "ampere/mesh/chi/types.h"

namespace ampere::mesh::chi {

class SnoopStatus
{
public:
    SnoopStatus();
    void set_state(NodeId agent_id, FinalState agent_state, bool stash);

    size_t count() const;

    void reset();

    FinalState get_state() const { return static_cast<FinalState>(state); }
    std::optional<NodeId> primary_snoop_target() const;
    bool proxy_owned() const { return po; }

    using STVec = std::array<NodeId, 2>;
    using NodeIdSet = std::set<NodeId>;

    NodeIdSet core_valid_as_set() const;

    bool core_is_valid(NodeId node_id) const;

    // New API for PxHA, which thinks about these fields a bit differently
    // Also, what we're currently calling "core valid" is known as the StVec in Matrix
    void set_state(FinalState new_state);

    void set_stvec(NodeId agent_id);
    void clear_stvec(NodeId agent_id);
    void clear_all_except(NodeId agent_id);
    void clear_all();
    void set_primary_snoop_target(NodeId agent_id);
    void pop_primary_snoop_target();
    void set_proxy_owned(bool po) { this->po = po; };

    friend std::ostream& operator<<(std::ostream& os, const SnoopStatus&);

#ifdef USE_BOOST_SERIALIZATION
    template<typename Archive>
    void save(Archive& ar, const unsigned int version) const
    {
        ar << pst;
        ar << state;
        ar << partial;
        ar << po;

        // Boost serialization is supposed to understand STL containers, but it doesn't
        // seem to understand this set. So we have to iterate manually.
        //
        ar << count();

        for (const auto& nid : core_valid_as_set())
            ar << nid;
    }

    template<typename Archive>
    void load(Archive& ar, const unsigned int version)
    {
        ar >> pst;

        int tmp_state;
        bool tmp_partial;
        bool tmp_po;

        ar >> tmp_state;
        ar >> tmp_partial;
        ar >> tmp_po;

        state = tmp_state;
        partial = tmp_partial;
        po = tmp_po;

        size_t core_valid_size;
        ar >> core_valid_size;

        while (core_valid_size--) {
            NodeId nid;
            ar >> nid;
            core_valid_insert(nid);
        }
    }
    BOOST_SERIALIZATION_SPLIT_MEMBER()
#endif // USE_BOOST_SERIALIZATION

private:
    std::variant<STVec, NodeIdSet> core_valid;
    NodeId pst;

    int state : 4;
    bool partial : 1;
    bool po : 1;

    void core_valid_clear();
    void core_valid_erase(NodeId agent_id);
    void core_valid_insert(NodeId agent_id);
};

} // namespace ampere::mesh::chi

template<>
struct fmt::formatter<ampere::mesh::chi::SnoopStatus> : fmt::ostream_formatter
{};

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
