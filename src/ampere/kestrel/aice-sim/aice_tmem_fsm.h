//
// Copyright (c) 2018-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once
#include <algorithm>
#include <cassert>
#include "ampere/sc/cycle.h"
#include "ampere/kestrel/aice-sim/aice_instruction.h"
#include "ampere/kestrel/aice-sim/aice_common.h"

namespace ampere::kestrel::aice {

struct tmem_fsm_t
{
    aice_instruction_t inst;
    unsigned tmem_fsm_instance;
    tmem_fsm_direction_t direction;
    addr_t memptr; // dst for IMR_SRC, AMR_SRC; src for IMR_DEST, IMR_SCALE_DEST
    imm_t reg_idx; // amr or imr
    unsigned nrows;
    unsigned stride;
    sc::Cycle delay_until{0}; // do not process before

    unsigned expected_transfers{0};
    unsigned transfer_bytes{0};   // bytes per transfer
    unsigned mem_offset_cur{0};   // memory row index
    unsigned request_counter{0};  // count of requests sent
    unsigned response_counter{0}; // count of responses received

    bool done_sending_requests{false};
    bool done_receiving_responses{false};
    bool written_back{false};

public:
    tmem_fsm_t(aice_instruction_t& inst,
               tmem_fsm_direction_t _dir,
               unsigned tmem_fsm_instance,
               addr_t _memptr, // tmem src or dst
               imm_t _reg_idx, // imr or amr
               unsigned _nrows,
               unsigned _stride,
               unsigned _naccumulators = 0)
        : inst(inst),
          tmem_fsm_instance(tmem_fsm_instance),
          direction(_dir),
          memptr(_memptr),
          reg_idx(_reg_idx),
          nrows(_nrows),
          stride(_stride)
    {
        mem_offset_cur = memptr;
        constexpr unsigned size_of_accumulator = 4;
        size_t source_bytes = 0;
        size_t source_bytes_per_row = 0;
        assert(nrows > 0);
        switch (direction) {
        case tmem_fsm_direction_t::IMR_SRC:
            expected_transfers = imr_row_bytes * nrows / store_transfer_bytes;
            transfer_bytes = store_transfer_bytes;
            break;
        case tmem_fsm_direction_t::AMR_SRC:
            assert(_naccumulators > 0);

            source_bytes_per_row = _naccumulators * size_of_accumulator;
            source_bytes = source_bytes_per_row * nrows;
            transfer_bytes = std::min(source_bytes_per_row, store_transfer_bytes);
            expected_transfers = source_bytes / transfer_bytes;
            break;
        case tmem_fsm_direction_t::AMR_SRC_BF16:
            assert(_naccumulators > 0);
            source_bytes_per_row = _naccumulators * size_of_accumulator / 2;
            source_bytes = source_bytes_per_row * nrows;
            transfer_bytes = std::min(source_bytes_per_row, store_transfer_bytes);
            expected_transfers = source_bytes / transfer_bytes;
            break;
        case tmem_fsm_direction_t::AMR_SRC_VECTOR:
            source_bytes = size_of_accumulator * nrows;
            expected_transfers = std::max(static_cast<size_t>(1), source_bytes / store_transfer_bytes);
            transfer_bytes = std::min(source_bytes, store_transfer_bytes);
            break;
        case tmem_fsm_direction_t::AMR_SRC_VECTOR_BF16:
            source_bytes = size_of_accumulator / 2 * nrows;
            expected_transfers = std::max(static_cast<size_t>(1), source_bytes / store_transfer_bytes);
            transfer_bytes = std::min(source_bytes, store_transfer_bytes);
            break;
        case tmem_fsm_direction_t::IMR_DEST:
            expected_transfers = imr_row_bytes * nrows / load_transfer_bytes;
            transfer_bytes = load_transfer_bytes;
            break;
        case tmem_fsm_direction_t::IMR_SCALE_DEST:
            expected_transfers = nrows;
            transfer_bytes = load_transfer_bytes;
            break;
        }
        assert(expected_transfers > 0);
        DBGN(2,
             "TMEM FSM{}: START {} expected_transfers: {}",
             tmem_fsm_instance,
             util::enum_to_string<tmem_fsm_direction_t>(direction),
             expected_transfers);
    }

    bool tally_mem_response()
    { // returns true when done
        DBGN(2,
             "TMEM FSM{}: {} response tracker: remaining_responses: {}",
             tmem_fsm_instance,
             util::enum_to_string<tmem_fsm_direction_t>(direction),
             expected_transfers - response_counter);
        AMPERE_ASSERT(!done_receiving_responses, "Received an unexpected response to a TMEM FSM");
        ++response_counter;
        if (response_counter == expected_transfers) {
            done_receiving_responses = true;
            return true;
        }
        return false;
    }

    unsigned get_tmem_fsm_instance() const { return tmem_fsm_instance; }
    aice_instruction_t& get_instruction() { return inst; }
    bool is_done_sending_requests() const { return done_sending_requests; }
    bool is_done() const { return done_sending_requests && done_receiving_responses; }
    bool is_written_back() const { return written_back; }
    void set_written_back() { written_back = true; }
    regidx_t get_reg_idx() const { return reg_idx; }
    unsigned get_requests_sent() const { return request_counter; }

    tmem_fsm_direction_t get_direction() const { return direction; }

    addr_t get_mem_ref() const
    { // call before stepping FSM
        return mem_offset_cur;
    }

    bool finished_requests() const
    {
        if (request_counter == expected_transfers) {
            return true;
        }
        return false;
    }
    bool finished_responses() const
    {
        if (response_counter == expected_transfers) {
            return true;
        }
        return false;
    }

    void status() const
    {
        DBGN(2,
             "TMEM FSM{}: {} {}",
             tmem_fsm_instance,
             util::enum_to_string<tmem_fsm_direction_t>(direction),
             done_sending_requests ? "Done sending requests" : "Still sending requests");
        if (direction == tmem_fsm_direction_t::IMR_DEST) {
            DBGN(2,
                 "TMEM FSM{}: {}",
                 tmem_fsm_instance,
                 done_receiving_responses ? "Done receiving responses" : "Still receiving responses");
        }
    }
    void step()
    { // we made a request so update our internal state
        AMPERE_ASSERT(!done_sending_requests, "Should not step a FSM that is done making requests");
        mem_offset_cur += stride;
        ++request_counter;
        if (request_counter == expected_transfers) {
            done_sending_requests = true;
        }
    }
};

} // namespace ampere::kestrel::aice
