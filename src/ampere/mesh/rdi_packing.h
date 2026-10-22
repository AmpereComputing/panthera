//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <bitset>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <vector>

#include "ampere/sc/sc.h"

namespace ampere::mesh::rdi {

using json = nlohmann::json;
using namespace sc;

//
// Maximum number of physical links. This only exists so we can build some structures
// (like bitsets) with compile time sizes. Otherwise it's arbitrary and can be raised
// if necessary.
//
const size_t MAX_LINKS = 4;

enum class MsgType {
    REQ,
    RSP,
    SNP,
    DAT,
    CRD
};
const size_t NUM_MSG_TYPES = 5;

//
// PackingInfo can store how many of various packet types can fit into a FLIT format,
// or it can store how many are currently inside a FLIT while it's being packed. It
// is used for both of these purposes: specifying rules, and tracking state.
//
union PackingInfo
{
    struct State
    {
        unsigned reqs : 2;
        unsigned rsps : 2;
        unsigned snps : 2;
        unsigned dats : 1;
        unsigned crds : 3;
        unsigned : 6;
    } s;

    uint16_t valid{0};
};

//
// A PackingRule basically defines a single possible FLIT format. It contains how
// many of each type of packet can fit (`allowed`) as well as which physical links
// this FLIT can travel on (`allowed_links`). The `from_json()` function is so we can
// build these from command line parameters.
//
struct PackingRule
{
    PackingInfo allowed;
    std::bitset<MAX_LINKS> links;

    friend void from_json(const json& j, PackingRule& rule)
    {
        rule.allowed.s.reqs = j.contains("Req") ? j["Req"].get<int>() : 0;
        rule.allowed.s.rsps = j.contains("Rsp") ? j["Rsp"].get<int>() : 0;
        rule.allowed.s.snps = j.contains("Snp") ? j["Snp"].get<int>() : 0;
        rule.allowed.s.dats = j.contains("Dat") ? j["Dat"].get<int>() : 0;
        rule.allowed.s.crds = j.contains("Crd") ? j["Crd"].get<int>() : 0;

        if (j.contains("Links")) {
            auto allowed_links = j["Links"].get<std::vector<int>>();

            for (auto l : allowed_links)
                rule.links[l] = true;
        } else {
            rule.links.flip();
        }
    }
};

//
// PackingRules is just the list of all possible FLIT formats. This is how it looks in
// a config file:
//
//      rdi.packer.rules [
//          { "Req": 1, "Rsp": 0, "Snp": 0, "Dat": 0, "Crd": 1, "Links": [0,1] },
//          { "Req": 0, "Rsp": 2, "Snp": 0, "Dat": 0, "Crd": 2, "Links": [0,1] },
//          { "Req": 0, "Rsp": 0, "Snp": 1, "Dat": 0, "Crd": 1, "Links": [0,1] },
//          { "Req": 0, "Rsp": 0, "Snp": 0, "Dat": 1, "Crd": 0, "Links": [2,3] }
//      ]
//
//  This set of rules describes four FLIT formats:
//
//      1. Allow one request and one credit return type (meaning any # of credits for one CHI channel),
//         only on physical links 0 or 1.
//      2. Allow two responses and two credit return types, only on physical links 0 or 1.
//      3. Allow one snoop and one credit return type, only on physical links 0 or 1.
//      4. Allow one data packet and nothing else, only on physical links 2 or 3.
//
using PackingRules = std::vector<PackingRule>;

//
// OneLinkState contains the current packing for a single physical link. We build
// this structure while packing. `allowed` comes from the Rule that we started with,
// and `current` represents what we've packed so far.
//
struct OneLinkState
{
    PackingInfo allowed;
    PackingInfo current;
};

//
// Here we have the current state of the whole set of physical links. You might
// think we could build this while packing and then throw it out, but the case of
// some packets taking more than one FLIT (data, namely), means that it helps to
// have some of the information from the last packing persist into the next. That
// way we can know that a link is still busy passing Data.
//
using LinkState = std::array<OneLinkState, MAX_LINKS>;

//
// TBD
//
struct IncomingMsg
{
    MsgType type;
    int tag;
};

using IncomingMsgVec = std::vector<IncomingMsg>;

//
// Representation of a new packing. `packing_list` is the list of which input FIFO
// packets need to be placed on which physical links. `new_state` is the final
// state of the links after this packing is applied.
//
struct Packing
{
    LinkState new_state;
    std::vector<std::tuple<IncomingMsg, int>> packing_list;

    Packing(const LinkState& new_state) : new_state{new_state}, packing_list{} {}
    Packing(const Packing&) = default;
};

//
// The Packer class itself.
//
class Packer : sc::Module
{
public:
    Packer(const sc_core::sc_module_name& name, size_t links) : Module{name}, links{links} {}

    Packing pack(const IncomingMsgVec& incoming, const LinkState& current_link_state);

    void end_of_elaboration() override;

protected:
    Param<PackingRules> rules{"rules", "Packing rules for different FLIT formats", "[]"_json};

    size_t links;

    std::pair<Packing, size_t> greedy_pack(const IncomingMsgVec& incoming, size_t index, const Packing& current_pack);

    using PossiblePacks = std::vector<Packing>;

    void try_unfull_flits(const Packing& pack, IncomingMsg msg, PossiblePacks& possibilities);
    void try_new_flits(const Packing& pack, IncomingMsg msg, PossiblePacks& possibilities);
    void try_packing_flit(const Packing& pack, size_t link, IncomingMsg msg, PossiblePacks& possibilities);
};

} // namespace ampere::mesh::rdi
