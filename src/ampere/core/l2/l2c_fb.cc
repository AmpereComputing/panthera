//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/core/l2/l2c_fb.h"
#include "ampere/core/l2/l2c_op.h"
#include <fmt/ostream.h>

namespace ampere::core::l2 {

using uarch_common::MemOp::StoreType;

void FillBufferEntry::merge_chunks_returned(uint8_t chunks)
{
    chunks_returned |= chunks;

    // Chunks returned implies valid bytes, but not the other way around (because
    // valid bytes can come from writes from the core as well).
    //
    for (uint64_t bytes = 0xffffull; chunks; chunks >>= 1, bytes <<= 16) {
        if (chunks & 1)
            merge_bytes(bytes);
    }
}

void FillBufferEntry::reset_srq()
{
    srq.tgt = mesh::chi::NodeId{0};
    srq.addr = 0x0_pa;
    srq.dbid = 0;
    srq.need_comp_ack = false;
    srq.sent_comp_ack = false;
    srq.comp_rcvd = false;
}

bool FillBufferEntry::returns_to_core() const
{
    for (auto& ret : return_data) {
        if (ret->origin == MemOp::ICF || ret->origin == MemOp::LSU || ret->origin == MemOp::DMMU
            || ret->origin == MemOp::IMMU)
            return true;
    }

    return false;
}

FillBufferEntry& FillBuffer::allocate(FBCredits credits, uint64_t byte_enables)
{
    int id;

    if (id_free_list.empty()) {
        id = static_cast<int>(entries.size());
        entries.resize(id + 1);
    } else {
        id = id_free_list.front();
        id_free_list.pop();
    }

    auto& entry = entries[id];

    entry.id = id;
    entry.line = Line{};
    entry.state = FillBufferEntry::State::ALLOCATED;
    entry.byte_enables = byte_enables;
    entry.chunks_returned = 0;
    entry.type = FillBufferEntry::Type::FB_NORMAL;
    std::swap(entries[id].credits, credits);
    entry.is_store_release = false;
    entry.is_pwc = false;

    entry.srq.allocated = false;
    entry.srq.need_comp_ack = false;
    entry.srq.sent_comp_ack = false;
    entry.srq.comp_rcvd = false;

    return entry;
}

void FillBuffer::deallocate(int id)
{
    auto& entry = entries[id];
    entry.state = FillBufferEntry::State::INVALID;
    entry.return_data.clear();
    entry.allocated_by_req.reset();
    entry.allocated_by_snp.reset();
    AMPERE_ASSERT(entry.credits.complete(),
                  "Accessory FB Credits should be explicitly released before FB deallocation");
    entry.credits.release();
    id_free_list.push(id);
}

boost::optional<FillBufferEntry&> FillBuffer::lookup(PhysAddr addr)
{
    for (auto& entry : entries) {
        if (entry.state != FillBufferEntry::State::INVALID && entry.state != FillBufferEntry::State::DEALLOC_READY
            && entry.state != FillBufferEntry::State::FILLED_PENDING_UPGRADE
            && entry.state != FillBufferEntry::State::EVICTING && addr == entry.line.addr)
        {
            return entry;
        }
    }

    return {};
}

/* Check that at least 1 FB entry corresponding to the input addr has the correct inclusivity flag.
 * There may be 2 FBs for the same addr -> only one with the correct L2/LSU inclusivity bit.
 * Scenario:
 * FB[0] (read request; state = SC) has already written back to LSU (hence contains inclusivity information) but hasn't
 * deallocated yet. A write request to the same address hits FB[0], upgrades it to FILLED_PENDING_UPGRADE, and allocates
 * FB[1] to obtain unique state.
 */
boost::optional<bool> FillBuffer::check_inclusion_bit(PhysAddr addr, bool iside) const
{
    bool hit = false;
    bool correct_flag = false;

    for (const auto& entry : entries) {
        if (entry.state != FillBufferEntry::State::INVALID && entry.state != FillBufferEntry::State::DEALLOC_READY
            && addr == entry.line.addr)
        {
            correct_flag |= (iside ? entry.line.in_l1i : entry.line.in_l1d);
            hit = true;
        }
    }

    if (hit)
        return correct_flag;

    return {};
}

/* Set inclusivity flag for all the FB entries corresponding to a given addr
 * (there may be multiple due to upgrades).
 */
void FillBuffer::set_inclusion_bit(PhysAddr addr, bool value, bool iside)
{
    for (auto& entry : entries) {
        if (entry.state != FillBufferEntry::State::INVALID && entry.state != FillBufferEntry::State::DEALLOC_READY
            && addr == entry.line.addr)
        {
            if (iside)
                entry.line.in_l1i = value;
            else
                entry.line.in_l1d = value;
        }
    }
}

void FillBuffer::srt_allocate(int id)
{
    AMPERE_ASSERT(!entries[id].srq.allocated,
                  "Attempted to double allocate system request entry for the identical fill buffers");
    entries[id].srq.allocated = true;
    srq_allocated++;
}

void FillBuffer::srt_deallocate(FillBufferEntry& entry)
{
    AMPERE_ASSERT(entry.srq.allocated,
                  "Attempted to double deallocate system request entry for the identical fill buffers");
    entry.srq.allocated = false;
    srq_allocated--;
}

unsigned FillBuffer::srt_count() const
{
    return srq_allocated;
}

unsigned FillBuffer::store_types() const
{
    unsigned flags{0};

    for (const auto& entry : entries) {
        // Once the data for a write miss has arrived in the FB, the store is globally observable and
        // a barrier that is waiting for it can proceed (on some projects).
        if (fill_releases_barrier && !entry.pending_fill())
            continue;

        // The CHI spec allows us to consider a store as observed as soon as we send the CompAck. After that,
        // even if we haven't yet received the data, we will stall snoop a snoop to that line until the data
        // is received, so nobody will get a stale response.
        if (comp_ack_releases_barrier && entry.srq.need_comp_ack && entry.srq.sent_comp_ack)
            continue;

        if (entry.state != FillBufferEntry::State::INVALID && entry.state != FillBufferEntry::State::DEALLOC_READY
            && entry.is_pwc)
        {
            flags |= StoreType::STORE_ANY | barrier_to_store_type(entry.allocated_by_req->barrier_type)
                     | (entry.atomic() ? StoreType::STORE_ATOMIC : 0);
        }
    }

    return flags;
};

bool FillBuffer::has_request_in_bank(PhysAddr addr, unsigned bank) const
{
    for (const auto& entry : entries)
        if (entry.state != FillBufferEntry::State::INVALID && entry.bank_id == static_cast<int>(bank)
            && entry.line.addr == addr)
            return true;

    return false;
}

const FillBufferEntry* FillBuffer::pending_atomic(unsigned bank) const
{
    for (const auto& entry : entries)
        if (entry.state != FillBufferEntry::State::INVALID && entry.bank_id == static_cast<int>(bank) && entry.atomic())
            return &entry;

    return nullptr;
}

int FillBuffer::has_pending_request(PhysAddr addr) const
{
    for (const auto& entry : entries) {
        if (entry.srq.addr == addr && entry.srq.allocated
            && (entry.state != FillBufferEntry::State::EVICTING || !entry.pending_req))
        {
            return entry.id;
        }
    }
    return -1;
}

/* Upon a fill corresponding to a given FB entry, check if there is another upgraded
 * FB entry with the same addr. If yes, overwrite the upgraded entry.
 * Scenario: write op hits an already-filled read op's FB entry.
 */
bool FillBuffer::overwrite_upgraded_fb_entry(int incoming_fill_id)
{
    auto& fbe = entries[incoming_fill_id];

    for (auto& tgt_entry : entries) {
        if (tgt_entry.id != fbe.id && tgt_entry.state == FillBufferEntry::State::FILLED_PENDING_UPGRADE
            && tgt_entry.line.addr == fbe.line.addr)
        {
            bool in_l1d = tgt_entry.line.in_l1d;
            bool in_l1i = tgt_entry.line.in_l1i;
            tgt_entry.line = fbe.line;
            // merge inclusivity (younger fbe entry may have been allocated after the inclusivity flag of the tgt fbe
            // was set)
            tgt_entry.line.in_l1d |= in_l1d;
            tgt_entry.line.in_l1i |= in_l1i;
            return true;
        }
    }

    return false;
}

void FillBuffer::dump(std::ostream& os) const
{
    fmt::print(os, "fill buffers:\n");
    fmt::print(os,
               "| Id |       Addr       | Line State |        FB State        | PWC | SRT Allocated | bank_id | "
               "stash_allocated \n");
    fmt::print(os, "===================================================================================\n");
    for (auto& fbe : entries) {
        if (fbe.state == FillBufferEntry::State::INVALID)
            continue;
        bool stash_allocated = fbe.line.cache_type == CacheType::L3;
        fmt::print(os,
                   "| {:2} | {:16} |     {:2}     | {:22} |  {:2} | {:3} | {:5} | {}\n",
                   fbe.id,
                   fbe.line.addr,
                   fbe.line.state,
                   fbe.state,
                   fbe.is_pwc ? 'X' : ' ',
                   fbe.srq.allocated,
                   fbe.bank_id,
                   stash_allocated);
    }
    fmt::print(os, "====================================================================\n");
}

unsigned FillBuffer::outstanding_reqs(mesh::chi::RequestOpcode chi_opcode) const
{
    unsigned reqs = 0;
    for (auto& fbe : entries) {
        if (fbe.state != FillBufferEntry::State::INVALID && fbe.state != FillBufferEntry::State::DEALLOC_READY
            && fbe.srq.chi_request == chi_opcode)
            reqs++;
    }
    return reqs;
}

} // namespace ampere::core::l2
