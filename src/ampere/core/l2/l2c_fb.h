//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <deque>
#include <vector>
#include <boost/optional.hpp>

#include "ampere/archx/addr.h"
#include "ampere/core/l2/l2c_line.h"
#include "ampere/core/l2/l2c_opcode.enum.h"
#include "ampere/mesh/chi/types.h"
#include "ampere/sc/credit.h"
#include "ampere/uarch_common/memop.h"
#include "ampere/uarch_common/memreq.h"
#include "ampere/mesh/mesh_snp.h"
#include "ampere/mesh/mesh_req.h"
#include "ampere/util/enum.h"
#include <optional>

make_ns_enum(ampere::core::l2, _FillBufferEntryState,
    INVALID,                 // FB is invalid
    ALLOCATED,               // FB has been allocated for a CHI request
    PENDING_UPGRADE,         // FB has been allocated and not filled and is being upgraded by a ReadUnique
    FILLED_PENDING_UPGRADE,  // FB has been filled and is being upgraded by a ReadUnique (similar to having a line in the array and Busy bit set)
    FILLED,                  // FB has been filled and is waiting to move line into array
    EVICTING,                // FB holds victim line and cannot be hit
    DEALLOC_READY            // FB has no data and is awaiting deallocation from SIU
);

make_ns_enum(ampere::core::l2, _FillBufferEntryType,
    FB_NORMAL,            // Is not anything below
    FB_LDEXCL,            // Is outstanding Load Exclusive
    FB_STEXCL,            // Is outstanding Store Exclusive
    FB_ATOMIC_LOAD,       // Is outstanding Atomic
    FB_ATOMIC_STORE,      // Is outstanding Atomic
    FB_CMO,               // Is outstanding CMO
    FB_TMO                // Is outstanding TMO
);

namespace ampere::core::l2 {

using namespace archx;
using namespace uarch_common;

struct FBCredits
{
    FBCredits()
        : unified_credit{},
          fill_address_credit{},
          eviction_address_credit{},
          eviction_data_credit{},
          write_data_credit{},
          split_eviction_data_credit{},
          split_write_data_credit{}
    {
    }
    FBCredits(Credit unified_credit,
              Credit fill_address_credit,
              Credit eviction_address_credit,
              Credit eviction_data_credit,
              Credit write_data_credit,
              Credit split_eviction_data_credit,
              Credit split_write_data_credit)
        : unified_credit{std::move(unified_credit)},
          fill_address_credit{std::move(fill_address_credit)},
          eviction_address_credit{std::move(eviction_address_credit)},
          eviction_data_credit{std::move(eviction_data_credit)},
          write_data_credit{std::move(write_data_credit)},
          split_eviction_data_credit{std::move(split_eviction_data_credit)},
          split_write_data_credit{std::move(split_write_data_credit)}
    {
    }

    Credit unified_credit;
    Credit fill_address_credit;
    Credit eviction_address_credit;
    // Data credits from unified data credit pool
    Credit eviction_data_credit;
    Credit write_data_credit;
    // Data credits from split pools
    Credit split_eviction_data_credit;
    Credit split_write_data_credit;

    operator bool() const { return (unified_credit || eviction_address_credit); }

    bool complete(void)
    {
        return !(fill_address_credit || eviction_data_credit || write_data_credit || split_eviction_data_credit
                 || split_write_data_credit);
    }

    void release(void)
    {
        if (unified_credit)
            release_credit(unified_credit);
        if (fill_address_credit)
            release_credit(fill_address_credit);
        if (eviction_address_credit)
            release_credit(eviction_address_credit);
        if (eviction_data_credit)
            release_credit(eviction_data_credit);
        if (write_data_credit)
            release_credit(write_data_credit);
        if (split_eviction_data_credit)
            release_credit(split_eviction_data_credit);
        if (split_write_data_credit)
            release_credit(split_write_data_credit);
    }
};

struct FillBufferEntry
{
    using State = _FillBufferEntryState;
    using Type = _FillBufferEntryType;

    int id;
    Line line;
    State state;
    uint64_t byte_enables;
    uint8_t chunks_returned;
    Type type;
    FBCredits credits;
    bool victim{false};
    int victim_way{-1};

    int bank_id{-1}; // for debug

    std::deque<MemReq> return_data;

    MemReq allocated_by_req;
    mesh::MeshSnp allocated_by_snp;

    bool is_store_release;
    bool is_pwc;

    std::optional<mesh::MeshReq> pending_req;

    // In the real machine, the System Request Queue is a separate queue, but it aligns exactly with the FBs. So
    // we just store the SRQ related information directly in the FB for the same effect.
    struct SRTEntry
    {
        bool allocated{false};
        mesh::chi::RequestOpcode chi_request;
        mesh::chi::NodeId tgt;
        PhysAddr addr;
        LineState state;
        uint16_t dbid;
        bool need_comp_ack{false};
        bool sent_comp_ack{false};
        bool comp_rcvd{false};
    } srq;

    void merge_bytes(uint64_t new_bytes) { byte_enables |= new_bytes; }
    void set_all_bytes_valid() { byte_enables = util::mask(PhysAddr::LINE_SIZE); }
    bool all_bytes_valid() const { return byte_enables == util::mask(PhysAddr::LINE_SIZE); }

    bool from_prefetch() const { return allocated_by_req && allocated_by_req->origin == uarch_common::MemOp::PREFETCH; }
    bool from_write() const { return allocated_by_req && allocated_by_req->is_write(); }
    bool atomic() const { return type == Type::FB_ATOMIC_LOAD || type == Type::FB_ATOMIC_STORE; }
    bool pending_fill() const
    {
        return state != State::FILLED && state != State::EVICTING && state != State::DEALLOC_READY;
    }
    void add_response(MemReq req) { return_data.push_back(req); }

    void merge_chunks_returned(uint8_t chunks);
    bool all_chunks_returned() const { return chunks_returned == 0xf; }
    void reset_srq();
    bool returns_to_core() const;

    Opcode fill_opcode() const
    {
        if (type == l2::FillBufferEntry::Type::FB_ATOMIC_LOAD)
            return l2::Opcode::ATOMIC_LOAD;
        else if (type == l2::FillBufferEntry::Type::FB_ATOMIC_STORE)
            return l2::Opcode::ATOMIC_STORE;
        return l2::Opcode::FILL;
    }
};

class FillBuffer
{
public:
    explicit FillBuffer()
    {
        AMPERE_ASSERT(!comp_ack_releases_barrier || fill_releases_barrier,
                      "comp_ack_releases_barrier not supported without fill_releases_barrier");
    }

    Param<bool> fill_releases_barrier{
        "fill_releases_barrier",
        "Barrier can proceed after store fill completes, instead of waiting for eviction complete",
        false};
    Param<bool> comp_ack_releases_barrier{
        "comp_ack_releases_barrier", "Barrier can proceed as soon as we set CompAck for a store", false};

    FillBufferEntry& allocate(FBCredits credits, uint64_t byte_enables = 0);
    void deallocate(int id);

    boost::optional<FillBufferEntry&> lookup(PhysAddr addr);

    boost::optional<bool> check_inclusion_bit(PhysAddr addr, bool iside) const;
    void set_inclusion_bit(PhysAddr addr, bool value, bool iside);

    bool overwrite_upgraded_fb_entry(int incoming_fill_id);

    FillBufferEntry& operator[](size_t index)
    {
        auto& fbe = entries.at(index);
        if (!fbe.credits)
            AMPERE_ASSERT(fbe.credits, "access of invalid FB[{}]", index);
        return fbe;
    }
    const FillBufferEntry& operator[](size_t index) const { return const_cast<FillBuffer*>(this)->operator[](index); }

    unsigned srt_count() const;
    void srt_allocate(int id);
    void srt_deallocate(FillBufferEntry& entry);

    bool has_request_in_bank(PhysAddr addr, unsigned bank) const;
    int has_pending_request(PhysAddr addr) const;
    const FillBufferEntry* pending_atomic(unsigned bank) const;

    unsigned store_types() const;

    void dump(std::ostream& os) const;

    unsigned outstanding_reqs(mesh::chi::RequestOpcode chi_opcode) const;

private:
    std::vector<FillBufferEntry> entries;
    std::queue<int> id_free_list;
    unsigned srq_allocated{0};
};

} // namespace ampere::core::l2
