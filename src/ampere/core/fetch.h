//
// Copyright (c) 2019-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_CORE_FETCH_H
#define AMPERE_CORE_FETCH_H

#include <boost/optional.hpp>
#include <cstdint>
#include <queue>

#include "ampere/archx/addr.h"
#include "ampere/core/prediction.h"
#include "ampere/core/fetch_id.h"
#include "ampere/sc/transaction.h"
#include "ampere/sc/report.h"
#include "ampere/sc/credit.h"
#include "ampere/util/enum.h"

make_ns_enum_with_type(ampere::core, EarlyPredictor, uint8_t,
    NP = 0,
    NNPP = 1,
    NNP = 2,
    NNNP = 3
);

namespace ampere::core {

using namespace ampere::archx;

using archx::BranchType;
using archx::PhysAddr;
using archx::ProgramCounter;
using BPUBank = size_t;

class FetchObj : public ampere::sc::Transaction
{
public:
    FetchObj(ProgramCounter pc, FetchId fetch_id)
        : program_counter{pc}, m_exit_pc{pc.next_line_address() - 4}, next_fetch_pc{pc}, fetch_id{fetch_id}
    {
    }

    FetchObj(ampere::sc::transaction_ptr<FetchObj>& fetch, FetchId fetch_id)
        : program_counter{fetch->program_counter},
          prev_program_counter{fetch->prev_program_counter},
          prediction{fetch->prediction},
          m_exit_pc{fetch->m_exit_pc},
          next_fetch_pc{fetch->program_counter},
          line_prediction{fetch->line_prediction},
          phys_addr{fetch->phys_addr},
          page_size{fetch->page_size},
          fetch_id{fetch_id},
          loop_start{fetch->loop_start},
          loop_collect{fetch->loop_collect},
          loop_end{fetch->loop_end},
          loop_abort{fetch->loop_abort},
          loop_iterations{fetch->loop_iterations},
          loop_restart_iterations{fetch->loop_restart_iterations}
    {
    }

    ProgramCounter next_program_counter() const;
    ProgramCounter cache_line() const { return program_counter.line_address(); }
    ProgramCounter instruction_pc(size_t instruction_index) const;
    ProgramCounter exit_pc() { return m_exit_pc; };
    size_t num_instructions() const;
    size_t instructions_fetched() const;
    void fetch_instruction(ProgramCounter inst_pc);

    BPUBank bank{0};
    const ProgramCounter program_counter;
    ProgramCounter prev_program_counter;
    ProgramCounter prev_program_counter_uncorrected;
    ProgramCounter prev_prev_program_counter;
    ProgramCounter prev_prev_program_counter_uncorrected;

private:
    Prediction prediction;
    ProgramCounter m_exit_pc;
    ProgramCounter next_fetch_pc;

public:
    const Prediction& get_prediction() const { return prediction; }
    void update_prediction_target(ProgramCounter target);
    void update_prediction(const Prediction& new_prediction, bool truncate = false);
    void update_prediction_keep_exit_pc(const Prediction& new_prediction);

    LinePrediction line_prediction;

    boost::optional<PhysAddr> phys_addr;
    PageSize::Size page_size{PageSize::Size::INVALID};
    FetchId fetch_id;
    sc::Credit feq_credit{nullptr};
    boost::optional<ClearType> clear_type;
    // If this prediction caused a frontend clear, but we were able to
    // immediately inject a nextline fetch, record that fetch ID here (ICF uses
    // this to know NOT to clear this fetch if received in the cycle following
    // a clear; all other fetches will be cleared that cycle)
    boost::optional<FetchId> post_clear_nextline_target_fetch_id;

    EarlyPredictor early_predictor{EarlyPredictor::NNP}; // which early predictor is/was used for this fetch
    bool nnp_hit{false};                                 // The N*P* lookup was a hit
    uint64_t nnp_set{0};                                 // The index of the N*P* lookup
    int nnp_way{0};                // If N*P* was a hit (nnp_hit), the way of the original tag match
    bool scheduled_it_pipe{false}; // Whether this has been scheduled on the IT (instruction tag) pipeline
    bool scheduled_id_pipe{false}; // Whether this has been scheduled on the ID (instruction data) pipeline
    bool delay_id_pipe{false};     // Was this fetch begun between a flush and the first following clear?
    bool ready{false};             // successfully looked up the I$ and is ready to be written to the IQ
    bool pending{false};           // missed the I$ and is waiting on a response from the L2
    bool fills_icache{false}; // allocated an IFB entry, and will deallocate the IFB entry when its last instruction is
                              // written to the IQ
    bool il0_hit{false};      // This fetch hit in the IL0
    bool victim_cache_hit{
        false}; // indicates `fills_icache` is true due to a hit in the victim cache rather than 'normal' L2C response
    bool missed_itlb{false};     // blocked on missing the ITLB (used for PMU tracking)
    bool missed_icache{false};   // blocked on missing the I$ (used for PMU tracking)
    bool missed_l2icache{false}; // blocked on missing the L2 I$ (used for PMU tracking)
    bool prefetch{false};
    bool two_cycle_iq_write{false}; // set to true if/when this fetch takes more than one cycle to write to the IQ

    bool loop_start{false};
    bool loop_collect{false};
    bool loop_end{false};
    bool loop_abort{false};
    size_t loop_iterations{0};
    size_t loop_restart_iterations{0};

    void fifo_clear()
    {
        if (feq_credit)
            release_credit(feq_credit);
    }

    void reset_icf_state()
    {
        scheduled_it_pipe = false;
        scheduled_id_pipe = false;
        delay_id_pipe = false;
        ready = false;
        pending = false;
        fills_icache = false;
        il0_hit = false;
        victim_cache_hit = false;
        missed_itlb = false;
        missed_icache = false;
        missed_l2icache = false;
        prefetch = false;
        two_cycle_iq_write = false;
    }

    size_t start_instruction_offset() { return (program_counter - cache_line()) / 4; }
};

using Fetch = ampere::sc::transaction_ptr<FetchObj>;

} // namespace ampere::core

#endif // AMPERE_CORE_FETCH_H
