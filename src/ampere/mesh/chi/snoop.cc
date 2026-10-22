//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/mesh/chi/snoop.h"
#include "ampere/sc/report.h"

namespace ampere::mesh::chi {

SnoopStatus::SnoopStatus() : state{static_cast<int>(FinalState::I)}, partial{false}, po{false} {}

size_t SnoopStatus::count() const
{
    if (std::holds_alternative<STVec>(core_valid)) {
        const auto& stvec = std::get<STVec>(core_valid);
        return (stvec[0].is_valid() ? 1 : 0) + (stvec[1].is_valid() ? 1 : 0);
    } else {
        return std::get<NodeIdSet>(core_valid).size();
    }
}

std::optional<NodeId> SnoopStatus::primary_snoop_target() const
{
    if (pst.is_valid())
        return pst;
    else
        return {};
}

bool SnoopStatus::core_is_valid(NodeId node_id) const
{
    if (std::holds_alternative<STVec>(core_valid)) {
        const auto& stvec = std::get<STVec>(core_valid);
        return stvec[0] == node_id || stvec[1] == node_id;
    } else {
        const auto& nset = std::get<NodeIdSet>(core_valid);
        return nset.find(node_id) != nset.end();
    }
}

void SnoopStatus::set_state(NodeId agent_id, FinalState agent_state, bool stash)
{
    auto current_state = static_cast<FinalState>(state);

    switch (agent_state) {
    case FinalState::I:
        if (pst == agent_id)
            pop_primary_snoop_target();
        else
            core_valid_erase(agent_id);

        if (!pst.is_valid())
            state = static_cast<int>(FinalState::I);

        break;

    case FinalState::SC:
        switch (current_state) {
        case FinalState::I:
            pst = agent_id;
            state = static_cast<int>(FinalState::SC);
            break;
        case FinalState::SC:
        case FinalState::SD:
            if (pst != agent_id)
                core_valid_insert(agent_id);
            break;
        case FinalState::UC:
        case FinalState::UD:
            if (pst == agent_id)
                state = static_cast<int>(FinalState::SC);
            else
                AMPERE_ERROR("mixed shared and unique states");
            break;
        }
        break;

    case FinalState::SD:
        switch (current_state) {
        case FinalState::I:
            pst = agent_id;
            state = static_cast<int>(FinalState::SD);
            break;
        case FinalState::SC:
            if (pst != agent_id) {
                auto old_pst = pst;
                pst = agent_id;
                core_valid_insert(old_pst);
            }
            state = static_cast<int>(FinalState::SD);
            break;
        case FinalState::SD:
            if (pst != agent_id)
                core_valid_insert(agent_id);
            break;
        case FinalState::UC:
        case FinalState::UD:
            if (pst == agent_id)
                state = static_cast<int>(FinalState::SD);
            else
                AMPERE_ERROR("mixed shared and unique states");
            break;
        }
        break;

    case FinalState::UC:
    case FinalState::UD:
        if (!pst.is_valid() || pst == agent_id) {
            pst = agent_id;
            state = static_cast<int>(agent_state);
        } else {
            AMPERE_ERROR("mixed shared and unique states");
        }
        break;
    }

    AMPERE_ASSERT(!pst.is_valid() || !core_is_valid(pst), "core cannot both be in STVec and PST");
}

void SnoopStatus::reset()
{
    core_valid_clear();
    pst = NodeId{};
    state = static_cast<int>(FinalState::I);
    partial = false;
    po = false;
    AMPERE_ASSERT(!pst.is_valid() || !core_is_valid(pst), "core cannot both be in STVec and PST");
}

void SnoopStatus::set_state(FinalState new_state)
{
    if (new_state == FinalState::I)
        reset();
    else
        state = static_cast<int>(new_state);
    AMPERE_ASSERT(!pst.is_valid() || !core_is_valid(pst), "core cannot both be in STVec and PST");
}

void SnoopStatus::set_stvec(NodeId agent_id)
{
    if (agent_id != pst)
        core_valid_insert(agent_id);
    AMPERE_ASSERT(!pst.is_valid() || !core_is_valid(pst), "core cannot both be in STVec and PST");
}

void SnoopStatus::clear_stvec(NodeId agent_id)
{
    if (agent_id == pst)
        pst = NodeId{};
    else
        core_valid_erase(agent_id);
    AMPERE_ASSERT(!pst.is_valid() || !core_is_valid(pst), "core cannot both be in STVec and PST");
}

void SnoopStatus::clear_all_except(NodeId agent_id)
{
    core_valid_clear();

    if (agent_id != pst) {
        pst = NodeId{};
        core_valid_insert(agent_id);
    }
    AMPERE_ASSERT(!pst.is_valid() || !core_is_valid(pst), "core cannot both be in STVec and PST");
}

void SnoopStatus::clear_all()
{
    core_valid_clear();
    pst = NodeId{};
    AMPERE_ASSERT(!pst.is_valid() || !core_is_valid(pst), "core cannot both be in STVec and PST");
}

void SnoopStatus::set_primary_snoop_target(NodeId agent_id)
{
    core_valid_erase(agent_id);
    if (pst.is_valid() && agent_id != pst) {
        auto tmp_pst = pst;
        pst = agent_id;
        core_valid_insert(tmp_pst);
    } else {
        pst = agent_id;
    }
    AMPERE_ASSERT(!pst.is_valid() || !core_is_valid(pst), "core cannot both be in STVec and PST");
}

void SnoopStatus::pop_primary_snoop_target()
{
    pst = NodeId{};

    if (count()) {
        if (std::holds_alternative<STVec>(core_valid)) {
            auto& stvec = std::get<STVec>(core_valid);
            pst = stvec[0];
            stvec[0] = NodeId{};

            if (stvec[1].is_valid()) {
                stvec[0] = stvec[1];
                stvec[1] = NodeId{};
            }
        } else {
            auto& nset = std::get<NodeIdSet>(core_valid);
            auto first_nid = *nset.begin();
            pst = first_nid;
            nset.erase(first_nid);

            if (nset.size() <= 2) {
                NodeIdSet tmp = nset;
                core_valid = STVec{};
                for (auto nid : tmp)
                    core_valid_insert(nid);
            }
        }
    }
    AMPERE_ASSERT(!pst.is_valid() || !core_is_valid(pst), "core cannot both be in STVec and PST");
}

void SnoopStatus::core_valid_clear()
{
    core_valid = STVec{};
    AMPERE_ASSERT(!pst.is_valid() || !core_is_valid(pst), "core cannot both be in STVec and PST");
}

void SnoopStatus::core_valid_erase(NodeId agent_id)
{
    if (std::holds_alternative<STVec>(core_valid)) {
        auto& stvec = std::get<STVec>(core_valid);

        if (stvec[0] == agent_id) {
            stvec[0] = stvec[1];
            stvec[1] = NodeId{};
        } else if (stvec[1] == agent_id) {
            stvec[1] = NodeId{};
        }
    } else {
        auto& nset = std::get<NodeIdSet>(core_valid);

        nset.erase(agent_id);

        if (nset.size() <= 2) {
            NodeIdSet tmp = nset;
            core_valid = STVec{};
            for (auto nid : tmp)
                core_valid_insert(nid);
        }
    }
    AMPERE_ASSERT(!pst.is_valid() || !core_is_valid(pst), "core cannot both be in STVec and PST");
}

void SnoopStatus::core_valid_insert(NodeId agent_id)
{
    AMPERE_ASSERT(pst != agent_id, "core cannot both be in STVec and PST");
    if (std::holds_alternative<STVec>(core_valid)) {
        auto& stvec = std::get<STVec>(core_valid);

        if (stvec[0] == agent_id || stvec[1] == agent_id)
            return;

        if (stvec[0].is_valid() && stvec[1].is_valid()) {
            NodeId a = stvec[0];
            NodeId b = stvec[1];
            core_valid = NodeIdSet{};
            auto& nset = std::get<NodeIdSet>(core_valid);
            nset.insert(a);
            nset.insert(b);
            nset.insert(agent_id);
        } else {
            if (stvec[0].is_valid())
                stvec[1] = stvec[0];
            stvec[0] = agent_id;
        }
    } else {
        std::get<NodeIdSet>(core_valid).insert(agent_id);
    }
    AMPERE_ASSERT(!pst.is_valid() || !core_is_valid(pst), "core cannot both be in STVec and PST");
}

auto SnoopStatus::core_valid_as_set() const -> NodeIdSet
{
    if (std::holds_alternative<STVec>(core_valid)) {
        NodeIdSet nset;
        const auto& stvec = std::get<STVec>(core_valid);
        if (stvec[0].is_valid())
            nset.insert(stvec[0]);
        if (stvec[1].is_valid())
            nset.insert(stvec[1]);
        return nset;
    } else {
        return std::get<NodeIdSet>(core_valid);
    }
}

std::ostream& operator<<(std::ostream& os, const SnoopStatus& snoop_status)
{
    fmt::print(os, "{}", snoop_status.get_state());

    if (snoop_status.pst.is_valid())
        fmt::print(os, " PST={}", snoop_status.pst);
    else
        fmt::print(os, " PST=None");

    size_t i = 0;

    for (const auto& agent_id : snoop_status.core_valid_as_set())
        fmt::print(os, " ST{}={}", i, agent_id);

    return os;
}

} // namespace ampere::mesh::chi

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
