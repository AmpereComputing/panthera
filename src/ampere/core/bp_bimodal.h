//
// Copyright (c) 2019, 2022, 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <nlohmann/json.hpp>

#include "ampere/core/prediction.h"
#include "ampere/core/predictor.h"
#include "ampere/util/bits.h"

namespace ampere::core {

using json = nlohmann::json;

using namespace ampere::archx;
using namespace ampere::sc;
using namespace ampere::util;

enum class BiModalDirection {
    NOT_TAKEN = 0,
    TAKEN = 1
};

enum class BiModalHysteresis {
    STRONG = 0,
    WEAK = 1
};

struct BiModalSet
{
    BiModalSet()
    {
        direction.fill(BiModalDirection::NOT_TAKEN);
        hysteresis.fill(BiModalHysteresis::WEAK);
    }
    std::array<BiModalDirection, 16> direction;
    std::array<BiModalHysteresis, 4> hysteresis;
};

void to_json(json& j, const BiModalSet& set);
void from_json(const json& j, BiModalSet& set);

class BPBiModal : public Module, public Predictor
{
public:
    explicit BPBiModal(const sc_core::sc_module_name& name, size_t bank_bits)
        : Module{name}, bank_bits{bank_bits}, set_bits{log2(size_t(num_sets))}
    {
        AMPERE_ASSERT((1UL << bank_bits) <= num_sets,
                      "The number of bank bits in the bimodal arrays cannot exceed the number of set bits");
    }

    Param<size_t> num_sets{"sets", "number of sets in the bi-modal", 16384};

    void predict(Fetch fetch, BranchHistory& history, const BranchContext& context) override;
    void train(const BranchInfo& br_info, BranchHistory& history, const BranchContext& context) override
    {
        train_impl(br_info, history, context, false);
    };
    void train_alt(const BranchInfo& br_info, BranchHistory& history, const BranchContext& context) override
    {
        train_impl(br_info, history, context, true);
    };
    bool strengthen(const BranchInfo& br_info, bool is_alternate);

    void save_state(json& j);
    void restore_state(const json& j);

protected:
    Event ev_full_update{"update", "A full update"};
    Event ev_strengthening_update{"strengthen", "A strengthening-only update"};
    EventData<int> ev_set{"set", "The bimodal set being accessed"};
    EventData<int> ev_offset{"offset", "The bimodal offset being accessed"};
    EventData<int> ev_hys_offset{"hys_offset", "The bimodal hystersis offset being accessed"};
    EventData<int> ev_taken{"taken", "Whether the entry will predict taken next time"};
    EventData<int> ev_strength{"strength", "Value of the applicable hysteresis bit"};
    EventData<bool> ev_is_alternate{"is_alternate", "Whether the array is being updated as an alternate"};

private:
    size_t compute_index(const Fetch& fetch, ProgramCounter pc) const;
    void train_impl(const BranchInfo& br_info, BranchHistory& history, const BranchContext& context, bool is_alternate);

    size_t bank_bits;
    size_t set_bits;
    std::vector<BiModalSet> sets{num_sets};
};

} // namespace ampere::core
