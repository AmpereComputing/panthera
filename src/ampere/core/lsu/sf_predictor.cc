//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/lsu/sf_predictor.h"

namespace ampere::core {

SFPredictor::SFPredictor(const sc_core::sc_module_name& name) : ManagedCache<ProgramCounter, SFPEntry>(name)
{
    this->perfect_value = SFPEntry(initial_value);
    if (tag_bits_param)
        ms_tag_bit = tag_bits_param + set_bits + line_bits - 1;
    else
        ms_tag_bit = 63;
}

SFPEntry SFPredictor::new_entry(unsigned initial_count, unsigned initial_pred = 0)
{
    return SFPEntry(initial_count, max_value, initial_pred, mispredict_max_value);
}

bool SFPredictor::predict(const Uop& uop)
{
    if (predict_wait_outside_el0
        && uop->instructions.front()->archx_instruction->state().exception_level() != archx::EL0)
        return true;

    auto aux_hit = aux_sfp.lookup(uop, uop->pc());
    if (aux_enable && aux_hit) {
        bool forward = aux_fwd_all || aux_hit->get_value() >= aux_forward_value;

        pmu_event(pmu_num_data(archx::PMU::LSU_SFP_AUX_HIT));
        predict_ev(uop, uop->pc(), forward_data(forward), aux_predict(true));

        return forward;
    } else {
        bool forward = initial_value >= forward_value;
        auto hit = lookup(uop, uop->pc());
        if (hit)
            forward = hit->get_value() >= forward_value;
        else
            refill(uop, uop->pc(), new_entry(initial_value));
        predict_ev(uop, uop->pc(), forward_data(forward), aux_predict(false));
        return forward;
    }
}

void SFPredictor::train(const Uop& uop, bool forward)
{
    auto counter_delta = forward ? 1 : -1;
    auto aux_hit = aux_sfp.lookup(uop, uop->pc());
    if (aux_enable && aux_hit) {
        (*aux_hit) += counter_delta;
        if (aux_hit->get_value() == 0) {
            aux_sfp.invalidate(uop, uop->pc());
            aux_ev(uop, uop->pc(), aux_activity(AUX_ACTIVITY::EVICT));
            pmu_event(pmu_num_data(archx::PMU::LSU_SFP_AUX_MISPRED));
            return;
        }
        aux_ev(uop, uop->pc(), aux_activity(AUX_ACTIVITY::TRAIN));
    } else {
        auto hit = lookup(uop, uop->pc());
        if (hit) {
            (*hit) += counter_delta;
            update_ev(uop, uop->pc(), counter_data(hit->get_value() + counter_delta));
        } else {
            auto new_counter = initial_value + counter_delta;
            refill(uop, uop->pc(), new_entry(new_counter));
            update_ev(uop, uop->pc(), counter_data(new_counter));
        }
    }
}

void SFPredictor::raw_scrub(const Uop& uop)
{
    if (reset_on_scrub)
        reset();

    auto aux_hit = aux_sfp.lookup(uop, uop->pc());
    if (aux_enable && aux_hit) {
        (*aux_hit)++;
        aux_ev(uop, uop->pc(), aux_activity(AUX_ACTIVITY::RAW_SCRUB));
    } else {
        auto hit = lookup(uop, uop->pc());
        if (hit) {
            if (aux_enable && hit->mispred.get_value() >= mispredict_threshold) {
                auto victim = aux_sfp.refill(uop, uop->pc(), SFPCounter(aux_initial_value, aux_max_value));
                hit->mispred.set_value(0);

                pmu_event(pmu_num_data(archx::PMU::LSU_SFP_AUX_ALLOC));
                aux_ev(uop, uop->pc(), aux_activity(AUX_ACTIVITY::INSERT), counter_data(aux_initial_value));
                if (victim) {
                    pmu_event(pmu_num_data(archx::PMU::LSU_SFP_AUX_EVICT));
                    aux_ev(uop, uop->pc(), aux_activity(AUX_ACTIVITY::EVICT));
                }
            } else {
                hit->set_to_max();
                hit->mispred++;
                update_ev(uop, uop->pc(), counter_data(hit->get_value()));
            }
        } else {
            refill(uop, uop->pc(), new_entry(max_value, 1));
            update_ev(uop, uop->pc(), counter_data(max_value));
        }
    }
}

void SFPredictor::reset()
{
    for (auto& set : this->get_sets())
        for (auto it = set.begin(); it != set.end(); ++it)
            it->second.value.set_value(initial_value);
}

void SFPredictor::dump(std::ostream& os) const
{
    uint idx{0};
    fmt::print(os, "= Contents of {} ====================================\n", this->name());
    fmt::print(
        os, "| {1:<{0}} | {2:<{0}} | {3:<{0}} | {4:<{0}} | \n", 9, "set index", "way num", "SFCounter", "mispred");
    for (auto& set : this->get_sets()) {
        // for the purpose of sorting according to way number:
        std::multimap<uint64_t, uint64_t> MM;

        // Insert every (key-value) pairs from
        // map M to multimap MM as (value-key)
        // pairs
        for (auto& it : set)
            MM.insert({it.second.way, it.first});

        // Print the multimap
        auto hit_entry = this->get_sets().at(idx);
        for (auto& it : MM) {
            auto it_hit = hit_entry.find(it.second);
            if (it_hit != hit_entry.end())
                fmt::print(os,
                           "| {1:<{0}} | {2:<{0}} | {3:<{0}} | {4:<{0}} | \n",
                           9,
                           idx,
                           it_hit->second.way,
                           it_hit->second.value.get_value(),
                           it_hit->second.value.mispred.get_value());
        }

        idx++;
    }
    if (aux_enable) {
        idx = 0;
        fmt::print(os, "= Contents of {} ====================================\n", aux_sfp.name());
        fmt::print(os, "| {1:<{0}} | {2:<{0}} | {3:<{0}} | \n", 9, "set index", "way num", "Conf");
        for (auto& set : aux_sfp.get_sets()) {
            // for the purpose of sorting according to way number:
            std::multimap<uint64_t, uint64_t> MM;

            // Insert every (key-value) pairs from
            // map M to multimap MM as (value-key)
            // pairs
            for (auto& it : set)
                MM.insert({it.second.way, it.first});

            // Print the multimap
            auto hit_entry = aux_sfp.get_sets().at(idx);
            for (auto& it : MM) {
                auto it_hit = hit_entry.find(it.second);
                if (it_hit != hit_entry.end()) {}
                fmt::print(os,
                           "| {1:<{0}} | {2:<{0}} | {3:<{0}} | \n",
                           9,
                           idx,
                           it_hit->second.way,
                           it_hit->second.value.get_value());
            }

            idx++;
        }
    }
    fmt::print(os, "============================================================\n");
}

} // namespace ampere::core

