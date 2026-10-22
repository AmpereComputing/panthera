//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once
#include <cstdint>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <fmt/ranges.h>
#include "ampere/sc/credit.h"

// #define DEBUG_GATHER_PROXY_CREDITS
#if defined(DEBUG_GATHER_PROXY_CREDITS)
#define DBG_GATHER(format_str, ...) fmt::print(stderr, format_str "\n", ##__VA_ARGS__)
#define DBG_GATHERC(format_str, ...) fmt::print(stderr, "{}: " format_str "\n", get_cycle(), ##__VA_ARGS__)
#else
#define DBG_GATHER(format_str, ...)
#define DBG_GATHERC(format_str, ...)
#endif

class GatherProxyCredit
{
    // txid of the ARM instr gather common to the 2 or 4 gather uops.
    std::uint64_t txid;

    unsigned id; // prfid id, cached because credit can get whacked on flushes
    unsigned commits;
    unsigned expected_commits;
    unsigned flushes;

    // count down for write-back of register for dependent uops.
    unsigned ixu_uses;

    // counts down number of chunks LSU needs to send before sending the final wake
    unsigned lsu_sent_counter;

    // alloc_id is the max of the 2 or 4 alloc_ids of the gather uops
    // for this ARM gather instruction. We connect consumers to that
    // max id. The last gather uop to write-back will use this id when
    // waking up consumers that their vector register is ready.
    std::uint64_t alloc_id;

public:
    GatherProxyCredit(std::uint64_t _txid, std::uint64_t _alloc_id, bool _gather2)
        : txid(_txid), id(0), commits(0), flushes(0), alloc_id(_alloc_id), prfid(nullptr)
    {
        ixu_uses = _gather2 ? 2 : 4;
        lsu_sent_counter = ixu_uses;
        expected_commits = ixu_uses;
        DBG_GATHER("Created gather proxy credit for arm instr txid: {}, alloc_id {}", txid, alloc_id);
    }

    ~GatherProxyCredit()
    {
        DBG_GATHER("Destroy gather proxy credit for arm instr txid: {}, alloc_id {}", txid, alloc_id);
    }

    // prfid represents the destination register, shared by the 2 or 4
    // gather uops used to implement the corresponding ARM gather
    // instruction.
    ampere::sc::Credit prfid{nullptr};

    // used for tracking/limiting the number of in-flight gathers
    ampere::sc::Credit gather_credit{nullptr};

    void set_gather_tbl_credit(ampere::sc::Credit _gather_credit)
    { // moving unique_ptr
        gather_credit = std::move(_gather_credit);
    }

    void set_prfid_credit(ampere::sc::Credit newprfid)
    { // moving unique_ptr
        prfid = std::move(newprfid);
        set_prfid_id();
    }

    void release_register_credit()
    {
        if (prfid) {
            release_credit(prfid);
        }
    }
    void release_gather_tbl_credit()
    {
        if (gather_credit) {
            release_credit(gather_credit);
        }
    }

    void record_commit() { commits++; }
    void flush()
    {
        if (flushes == 0) {
            release_register_credit();
            release_gather_tbl_credit();
        }
        flushes++;
    }

    bool done_commiting() const { return commits == expected_commits; }

    bool prfid_still_valid() const { return prfid ? true : false; }
    bool gather_tbl_credit_still_valid() const { return gather_credit ? true : false; }

    void set_prfid_id() { id = prfid->get_id(); }

    std::uint64_t get_prfid_id() const { return id; }

    std::uint64_t get_txid() const { return txid; }
    std::uint64_t get_alloc_id() const { return alloc_id; }

    void set_alloc_id(std::uint64_t id)
    {
        // take the max alloc_id and use that for writebacks.  gathers
        // loads can finish in any order. Only the temporally last
        // gather load actually wakes consumers. To have a consistent
        // alloc_id for waking up consumers, we force all gather loads
        // to use the largest alloc_id when writing back. The
        // consumers also expect that alloc_id since it looks like the
        // last writer in the incoming instruction stream.

        if (id > alloc_id) {
            DBG_GATHER("Updating gather alloc_id from {} to {}", alloc_id, id);
            alloc_id = id;
        }
    }

    unsigned get_uses_ixu() const { return ixu_uses; }

    void remove_use_ixu()
    {
        DBG_GATHER("Removing ixu use: txid: {}, alloc_id {} uses_before ixu: {}", txid, alloc_id, ixu_uses);
        AMPERE_ASSERT(ixu_uses > 0, "Trying to remove gather proxy credit use when no users");
        ixu_uses--;
    }

    bool done_ixu() const
    { // we can wake up ixu consumers
        return ixu_uses == 0;
    }

    bool commit_try_finish()
    {
        // return true if done committing gather uops
        record_commit();
        if (done_commiting()) {
            release_gather_tbl_credit();
            return true;
        }
        return false;
    }

    // Returns true after deciding to send last chunk to FSU
    bool lsu_done() const { return lsu_sent_counter == 0; }
    // Used when LSU sends a chunk to FSU
    void lsu_sent()
    {
        AMPERE_ASSERT(lsu_sent_counter > 0, "lsu_sent_counter should not underflow");
        lsu_sent_counter--;
    }
};

using GatherProxyCreditPtr = std::shared_ptr<GatherProxyCredit>;

