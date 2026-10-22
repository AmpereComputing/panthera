//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/mesh/rdi_packing.h"

namespace ampere::mesh::rdi {

Packing Packer::pack(const IncomingMsgVec& incoming, const LinkState& current_link_state)
{
    return greedy_pack(incoming, 0, current_link_state).first;
}

void Packer::end_of_elaboration()
{
    AMPERE_ASSERT(!rules.val().empty(), "{}: no rules; cannot pack RDI link", name());
}

std::pair<Packing, size_t> Packer::greedy_pack(const IncomingMsgVec& incoming,
                                               size_t index,
                                               const Packing& current_pack)
{
    if (index == incoming.size())
        return {current_pack, index};

    auto next_msg = incoming[index];

    Packing best_pack = current_pack;
    size_t best_index = index;

    PossiblePacks possibilities;

    try_unfull_flits(current_pack, next_msg, possibilities);
    try_new_flits(current_pack, next_msg, possibilities);

    // NOTE: Would it be worthwhile to check beyond index if `possibilities` was empty?
    // I'm thinking of cases where a particular packet might not be packable this time
    // around, but some of the ones afterward might be, e.g.:
    //
    //       +-- not packable
    //       |    +----+------ maybe packable
    //       v    v    v
    //    [ DAT, RSP, RSP ]
    //
    // If so, when do we stop? Do we just exaust `incoming`? And does this fundamentally
    // break our notion of priority? I.e., maybe we just end up in a starvation case
    // where later packets keep earlier ones from ever being packed...
    //
    for (auto& possibility : possibilities) {
        auto [new_pack, new_index] = greedy_pack(incoming, index + 1, possibility);

        if (new_index > best_index) {
            best_index = new_index;
            best_pack = new_pack;
        }
    }

    return {best_pack, best_index};
}

void Packer::try_unfull_flits(const Packing& pack, IncomingMsg msg, PossiblePacks& possibilities)
{
    for (size_t l = 0; l < links; l++) {
        if (pack.new_state[l].current.valid != 0)
            try_packing_flit(pack, l, msg, possibilities);
    }
}

void Packer::try_new_flits(const Packing& pack, IncomingMsg msg, PossiblePacks& possibilities)
{
    for (size_t l = 0; l < links; ++l) {
        if (pack.new_state[l].current.valid == 0) {
            for (auto& rule : rules.val()) {
                bool can_pack_type = ((msg.type == MsgType::REQ && rule.allowed.s.reqs)
                                      || (msg.type == MsgType::RSP && rule.allowed.s.rsps)
                                      || (msg.type == MsgType::SNP && rule.allowed.s.snps)
                                      || (msg.type == MsgType::DAT && rule.allowed.s.dats)
                                      || (msg.type == MsgType::CRD && rule.allowed.s.crds));

                bool can_pack_link = rule.links[l];

                if (can_pack_type && can_pack_link) {
                    Packing new_pack = pack;
                    new_pack.new_state[l].allowed = rule.allowed;
                    try_packing_flit(new_pack, l, msg, possibilities);
                }
            }
        }
    }
}

void Packer::try_packing_flit(const Packing& pack, size_t link, IncomingMsg msg, PossiblePacks& possibilities)
{
    auto& flit = pack.new_state[link];

    switch (msg.type) {
    case MsgType::REQ:
        if (flit.current.s.reqs < flit.allowed.s.reqs) {
            Packing new_pack = pack;
            ++new_pack.new_state[link].current.s.reqs;
            new_pack.packing_list.emplace_back(msg, link);
            possibilities.push_back(new_pack);
        }
        break;
    case MsgType::RSP:
        if (flit.current.s.rsps < flit.allowed.s.rsps) {
            Packing new_pack = pack;
            ++new_pack.new_state[link].current.s.rsps;
            new_pack.packing_list.emplace_back(msg, link);
            possibilities.push_back(new_pack);
        }
        break;
    case MsgType::SNP:
        if (flit.current.s.snps < flit.allowed.s.snps) {
            Packing new_pack = pack;
            ++new_pack.new_state[link].current.s.snps;
            new_pack.packing_list.emplace_back(msg, link);
            possibilities.push_back(new_pack);
        }
        break;
    case MsgType::DAT:
        if (flit.current.s.dats < flit.allowed.s.dats) {
            Packing new_pack = pack;
            ++new_pack.new_state[link].current.s.dats;
            new_pack.packing_list.emplace_back(msg, link);
            possibilities.push_back(new_pack);
        }
        break;
    case MsgType::CRD:
        if (flit.current.s.crds < flit.allowed.s.crds) {
            Packing new_pack = pack;
            ++new_pack.new_state[link].current.s.crds;
            new_pack.packing_list.emplace_back(msg, link);
            possibilities.push_back(new_pack);
        }
        break;
    }
}

} // namespace ampere::mesh::rdi
