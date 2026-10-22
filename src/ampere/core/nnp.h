//
// Copyright (c) 2020-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#ifndef AMPERE_CORE_NNP_H
#define AMPERE_CORE_NNP_H

#include "ampere/archx/addr.h"
#include "ampere/archx/branch_type.h"
#include "ampere/core/fetch.h"
#include "ampere/core/prediction.h"
#include "ampere/uarch_common/cache.h"

namespace ampere::core {

using namespace ampere::archx;
using namespace ampere::sc;
using namespace ampere::uarch_common;

// Note that an NNPEntry is *almost* the same as a Prediction, but it needs to store the offset as an
// integer rather than storing the branch PC directly. This is because it might alias to a different
// cache line and the original branch_pc might actually be outside the cache line being predicted.
//
struct NNPEntry
{
    uint64_t tag{0};
    BranchType branch_type{BranchType::NONE};
    ProgramCounter target{0};
    bool target_nextline{false};
    unsigned int offset{0};
    bool useful{false};
};

class NNP : CacheConfiguration
{
public:
    NNP(const sc_core::sc_module_name& name);
    void end_of_elaboration() override;
    Param<bool> act_as_np{"act_as_np", "act as next predictor rather than a next-next predictor", false};
    Param<bool> act_as_nnnp{"act_as_nnnp", "act as next-next-next predictor rather than a next-next predictor", false};
    Param<bool> correct_prev_pc{
        "correct_prev_pc", "Use the 'corrected' previous program counter upon flush and BTC clear", false};
    Param<size_t> tag_bits_param{"tag_bits", "tag bits used", 12};
    Param<size_t> bottom_set_bit{"bottom_set_bit", "bottom bit to use for set calculation", 6};
    Param<bool> update_hit_before_victim{"update_hit_before_victim",
                                         "Update the hit-upon NNP way (if there is one) before updating the victim way",
                                         false};
    Param<bool> allocate_useful{"allocate_useful", "Allocate new entries as useful", true};
    Param<bool> connor_index_hash{"connor_index_hash", "Use the new Connor index hash", false};

    boost::optional<NNPEntry&> lookup(Fetch fetch);
    void update(Fetch fetch, bool useful);

private:
    void allocate(Fetch& fetch, const Prediction& pred, uint64_t index, int way);
    void full_update(Fetch fetch, const Prediction& value);
    void set_useful(Fetch fetch);
    void weaken_useful(Fetch& fetch, uint64_t index, int way);
    void invalidate(Fetch& fetch, uint64_t index, int way);

protected:
    Event access_event{"access", "the NNP was looked up"};
    Event update_event{"update", "the NNP was updated"};
    Event set_useful_event{"set_useful", "the NNP was updated to indicate this entry was useful"};
    Event allocate_event{"allocate", "A new NNP entry was updated"};
    Event invalidate_event{"invalidate", "the NNP entry invalidated"};
    EventData<ProgramCounter> entrance_pc{"entrance_pc", "Prediction entrance PC"};
    EventData<ProgramCounter> br_pc{"br_pc", "PC of taken branch"};
    EventData<ProgramCounter> br_target{"br_target", "Branch target"};
    EventData<bool> br_target_nextline{"br_target_nextline", "Prediction at branch target is nextline"};
    EventData<int> br_type{"br_type", "Branch type"};
    EventData<bool> useful_bit{"useful", "NNP useful bit is now set"};
    EventData<bool> nnp_hit{"nnphit", "NNP access was a hit"};
    EventData<unsigned> nnp_set{"set", "NNP set"};
    EventData<unsigned> nnp_way{"way", "NNP way"};
    EventData<unsigned> nnp_tag{"tag", "NNP tag"};

    using SetType = std::vector<NNPEntry>;
    std::vector<SetType> array;

    Submodule<CacheReplacementPolicy, LRU> crp{"cache_replacement_policy"};

    std::pair<uint64_t, int> lookup_internal(Fetch fetch) const;
    uint64_t compute_set(ProgramCounter address) const;
    uint64_t compute_tag(ProgramCounter address) const;
};

} // namespace ampere::core

#endif // AMPERE_CORE_NNP_H
