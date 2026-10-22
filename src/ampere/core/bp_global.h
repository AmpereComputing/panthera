//
// Copyright (c) 2020, 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <nlohmann/json.hpp>

#include "ampere/core/predictor.h"
#include "ampere/sc/sc.h"

namespace ampere::core {

using json = nlohmann::json;

using namespace ampere::archx;
using namespace ampere::sc;

using BPArray = unsigned int;

struct BPIndex
{
    BPArray array;
    BPSet set;
    BPTag tag;
    int offset;
};

class StrengthCounter : public SaturatingCounter
{
public:
    StrengthCounter() : SaturatingCounter{3} {}
    StrengthCounter(BranchStrength s) : SaturatingCounter{0, 3, static_cast<unsigned>(s)} {}
    void operator=(BranchStrength s) { set_value(static_cast<unsigned>(s)); }
    BranchStrength get_strength() const { return static_cast<BranchStrength>(get_value()); }
};

void to_json(json& j, const StrengthCounter& counter);
void from_json(const json& j, StrengthCounter& counter);

class UseAlternateCounter : public SaturatingCounter
{
public:
    UseAlternateCounter() : SaturatingCounter{0, 31, 16} {}
};

void to_json(json& j, const UseAlternateCounter& counter);
void from_json(const json& j, UseAlternateCounter& counter);

struct BPGlobalEntry
{
    BPGlobalEntry() : tag{0}, el{EL0}, offset{0}, type{BranchType::NONE}, target{0_pc}, taken{false}, useful{false} {}

    BPGlobalEntry(BPTag tag, archx::EL el, int offset, bool taken, ProgramCounter target)
        : tag{tag}, el{el}, offset{offset}, type{BranchType::NONE}, target{target}, taken{taken}, useful{false}
    {
    }

    BPGlobalEntry(BPTag tag, archx::EL el, int offset, bool taken, ProgramCounter target, BranchStrength s)
        : tag{tag},
          el{el},
          offset{offset},
          type{BranchType::NONE},
          target{target},
          strength{s},
          taken{taken},
          useful{false}
    {
    }

    BPTag tag;
    archx::EL el;
    int offset;

    BranchType type;
    ProgramCounter target;
    bool target_nextline{false};
    StrengthCounter strength;

    bool taken;
    bool useful;

    bool most_recently_allocated{false};
};

void to_json(json& j, const BPGlobalEntry& entry);
void from_json(const json& j, BPGlobalEntry& entry);

using BPGlobalSet = std::vector<BPGlobalEntry>;
using BPGlobalArray = std::vector<BPGlobalSet>;
using RotationSelect = std::function<BPArray(Fetch, ProgramCounter, BranchHistory&)>;

struct IUMEntry : public BPGlobalEntry
{
    IUMEntry(BPSet set,
             BPTag tag,
             archx::EL el,
             int offset,
             bool taken,
             ProgramCounter target,
             BranchStrength s,
             std::uint64_t alloc_id)
        : BPGlobalEntry{tag, el, offset, taken, target, s}, set{set}, alloc_id{alloc_id}
    {
    }

    std::uint64_t age{0};
    BPSet set;
    std::uint64_t alloc_id;
};

class BPGlobal : public Module, public Predictor
{
public:
    BPGlobal(const sc_core::sc_module_name& name,
             std::vector<BPGlobalArray>& physical_arrays,
             int set_bits,
             int tag_bits,
             size_t bank_bits);

    Param<unsigned> history_size{"history_size", "number of historical branches to consider", 8};
    Param<size_t> ium_entries{"ium_entries", "number of entries in the IUM", 0};
    Param<bool> el_tagged{"el_tagged", "Global entries are tagged by EL in addition to normal tag", false};
    Param<size_t> gate_after_predictions_since_usage{
        "gate_after_predictions_since_usage",
        "Clock gate this global array after this many predictions have been seen since the last correct commit or "
        "allocation (0 disables)",
        0};

    void predict(Fetch fetch, BranchHistory& history, const BranchContext& context) override;
    void train(const BranchInfo& br_info, BranchHistory& history, const BranchContext& context) override;
    void train_alt(const BranchInfo& br_info, BranchHistory& history, const BranchContext& context) override;
    void recover(const Flush& flush_info,
                 BranchInfo* br_info,
                 BranchHistory& history,
                 const BranchContext& context) override;
    bool allocate(const BranchInfo& br_info,
                  BranchHistory& history,
                  const BranchContext& context,
                  bool force = false,
                  bool reset_useful = false);
    void clear_ium(std::uint64_t alloc_id = std::numeric_limits<std::uint64_t>::max());
    void commit(const BranchInfo& br_info, bool is_alternate) override;

    void set_rotation_select(RotationSelect func);
    void set_use_alternate_counter(std::shared_ptr<UseAlternateCounter> counter);

protected:
    std::shared_ptr<UseAlternateCounter> use_alternate_counter;

    Event br_global_read{"read", "physical read of a global array (for counting power)"};
    Event br_global_lookup{"lookup", "look up a global array"};
    Event br_global_update{"update", "update global array counters"};
    Event br_global_update_miss{"update_miss", "tried to update global array, but missed"};
    Event br_global_allocate{"allocate", "allocate an entry in the global array"};
    Event br_ium_allocate{"ium_allocate", "allocate an IUM entry in the global array"};
    Event br_ium_override{"ium_override", "used IUM entry to override prediction"};

    EventData<ProgramCounter> br_pc{"br_pc", "PC of branch instruction"};
    EventData<ProgramCounter> br_target{"br_target", "Branch target"};
    EventData<bool> br_target_nextline{"br_target_nextline",
                                       "Cacheline at branch target is guaranteed to be a nextline prediction"};
    EventData<bool> br_taken{"taken", "Branch was taken"};
    EventData<bool> br_useful{"useful", "Prediction was useful"};
    EventData<int> br_strength{"strength", "Prediction strength"};
    EventData<std::uint64_t> br_ium{"ium", "Prediction was made by the IUM with this alloc_id"};

    EventData<int> br_global_array{"array", "global array number"};
    EventData<int> br_global_set{"set", "set in the global array"};
    EventData<int> br_global_tag{"tag", "tag in the global array"};
    EventData<int> br_global_offset{"offset", "offset (instruction number) in a cache line"};

    void update_useful(BPGlobalEntry& entry,
                       bool primary_correct,
                       bool alternate_correct,
                       BranchStrength primary_strength,
                       BranchStrength alternate_strength);
    void update_use_alternate_counter(bool primary_correct, bool alternate_correct);

    std::pair<BPGlobalSet&, BPIndex> find_set(Fetch fetch, ProgramCounter branch_pc, BranchHistory& history);
    std::pair<BPGlobalEntry*, BPIndex> find_entry(Fetch fetch,
                                                  ProgramCounter branch_pc,
                                                  BranchHistory& history,
                                                  const BranchContext& context);
    BPIndex compute_index(const Fetch& fetch, ProgramCounter pc, BranchHistory& history) const;

    std::vector<BPGlobalArray>& physical_arrays;

    using IUM = std::map<std::tuple<BPSet, BPTag, archx::EL, int>, IUMEntry>;
    using IUMIter = IUM::iterator;
    IUM ium;

    virtual bool predict_entry(SlotPrediction& slot_prediction, const BPGlobalEntry& entry, IUMIter ium_it);
    virtual void train_entry(const BranchInfo& br_info, BPGlobalEntry& entry);

    RotationSelect select_rotation;

    size_t predictions_since_usage{0};
    bool clock_gated{false};

    int set_bits;
    int tag_bits;
    size_t bank_bits;
};

class BPGlobalIndirect : public BPGlobal
{
public:
    using BPGlobal::BPGlobal;

    void train_alt(const BranchInfo& br_info, BranchHistory& history, const BranchContext& context) override;

protected:
    bool predict_entry(SlotPrediction& p, const BPGlobalEntry& entry, IUMIter ium_it) override;
    void train_entry(const BranchInfo& br_info, BPGlobalEntry& entry) override;
};

} // namespace ampere::core
