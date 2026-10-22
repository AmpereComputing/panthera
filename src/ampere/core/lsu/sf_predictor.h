//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/sc/sc.h"
#include "ampere/core/uop.h"
#include "ampere/uarch_common/cache.h"

make_ns_enum(ampere::core, AUX_ACTIVITY,
    INSERT = 0,
    EVICT = 1,
    TRAIN = 2,
    RAW_SCRUB = 3
);

namespace ampere::core {

using namespace ampere::sc;
using namespace archx;
using namespace uarch_common;

class SFPCounter : public SaturatingCounter
{
public:
    SFPCounter(unsigned initial_count = 2, unsigned max_count = 3) : SaturatingCounter{0, max_count, initial_count} {}
};

class SFPEntry : public SFPCounter
{
public:
    SFPEntry(unsigned initial_count = 2, unsigned max_count = 3, unsigned initial_pred = 0, unsigned max_pred = 1)
        : SFPCounter(initial_count, max_count)
    {
        mispred = SFPCounter(initial_pred, max_pred);
    }
    SFPCounter mispred;
};

class SFPredictor : public ManagedCache<ProgramCounter, SFPEntry>
{
public:
    explicit SFPredictor(const sc_core::sc_module_name& name);

    Index compute_set(ProgramCounter address) const override
    {
        if (set_bits == 0)
            return 0;
        if (hash_address)
            return util::fold_to_n_bits(address.extract_range(63, 2), set_bits);
        auto index = address.extract_range(line_bits + set_bits - 1, line_bits);
        if (index >= num_sets)
            AMPERE_ERROR("set index is larger than the number of sets");
        return index;
    }

    uint64_t compute_tag(ProgramCounter address) const override
    {
        if (num_ways == 1)
            return 0;
        return address.extract_range(ms_tag_bit, line_bits + set_bits);
    }

    bool predict(const Uop& uop);
    void train(const Uop& uop, bool forward);
    void raw_scrub(const Uop& uop);
    SFPEntry new_entry(unsigned initial_count, unsigned initial_pred);

protected:
    unsigned ms_tag_bit;
    void dump(std::ostream& os = std::cout) const override;

private:
    Cache<ProgramCounter, SFPCounter> aux_sfp{"aux_sfp"};

    Param<bool> aux_enable{"aux_enable", "aux sf predictor enable", false};
    Param<bool> aux_fwd_all{
        "aux_fwd_all", "predict forward for all entries in aux sf predictor regardless of the counter value", true};
    Param<unsigned> mispredict_threshold{
        "mispredict_threshold", "number of raw_scrubs on the same entry before we insert in the aux table", 1};
    Param<unsigned> aux_forward_value{"aux_forward_value", "value to determine if forward is predicted", 1};
    Param<unsigned> aux_initial_value{"aux_initial_value", "Aux table entry initial value", 1};
    Param<unsigned> aux_max_value{"aux_max_value", "Max counter value for aux table entry", 1};
    Param<unsigned> mispredict_max_value{
        "mispredict_max_value", "Max counter value main table mispredict (raw scrub) counter", 1};
    Param<bool> hash_address{"hash_address", "hash address to compute set index", false};
    Param<unsigned> initial_value{"initial_value", "Main table initial counter value", 2};
    Param<unsigned> max_value{"max_value", "Main table max counter value", 3};
    Param<bool> reset_on_scrub{"reset_on_scrub", "reset all counters to reset value on a RAW scrub", false};
    Param<unsigned> forward_value{"forward_value", "value to determine if forward is predicted", 2};
    Param<bool> predict_wait_outside_el0{
        "predict_wait_outside_el0", "Always predict wait when not in EL0 to mimic current Linux usage of SSBS", true};

    Param<size_t> tag_bits_param{"tag_bits", "tag bits used", 0};
    void reset();

    Event aux_ev{"aux", "aux table activity"};
    EventData<AUX_ACTIVITY> aux_activity{"activity", "Insert, Evict, Train or Raw_scrub"};
    EventData<bool> aux_predict{"aux_predict", "Auxiliary table predicted this prediction"};

    Event predict_ev{"predict", "Make a store forward prediction"};
    Event update_ev{"update", "Update the store forward predictor"};
    EventData<bool> forward_data{"forward", "Store forward predicted"};
    EventData<unsigned> counter_data{"counter", "SF predictor counter value"};
    EventData<unsigned> index_data{"index", "SF predictor index number"};

    Event pmu_event{"pmu", "A PMU event has occurred"};
    EventData<PMU> pmu_num_data{"event", "PMU event number"};
};

} // namespace ampere::core
