//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <signal.h>
#include "ampere/mesh/pha_retry.h"
#include "ampere/mesh/pha_tracker.h"
#include "ampere/mesh/pha.h"

namespace ampere::mesh {

struct tracker_entry_state_transition_error : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

PHATracker::PHATracker(const sc_core::sc_module_name& name, size_t size, const PHA* pha)
    : Module{name}, size{size}, pha{pha}
{
    free_list.resize(size);
    std::iota(free_list.begin(), free_list.end(), 0);

    for (size_t i = 0; i < size; i++)
        entries.emplace_back(i);
}

PHATrackerEntry* PHATracker::allocate(MeshReq req, PHATrackerEntry& originator, uint8_t entry_type)
{
    auto entry_ptr = allocate(req, originator.input.PHA, entry_type, true);
    auto& entry = *entry_ptr;

    entry.sf_set = originator.sf_set;
    entry.sf_way = originator.sf_way;
    std::swap(entry.snoop_address, originator.snoop_address);
    std::swap(entry.snoop_status, originator.snoop_status);
    entry.sfvwake = &originator;
    block_entry(originator);
    TRACE(originator, "Entry {} is being blocked due to SFV", originator.id);
    return &entry;
}

PHATrackerEntry* PHATracker::allocate(MeshReq req, pha_fsm::PHA_t pha_type, uint8_t entry_type, bool sfv)
{
    auto& entry = get_free_tracker_entry();

    /* Populate tracker entry with inputs */
    init_from_req(entry, req, pha_type);
    init_common(entry, pha_type, entry_type);
    if (sfv)
        sfv_alloc(entry, req->Addr);
    else
        block_if_active(entry, req->Addr, false);

    return &entry;
}

void PHATracker::block_entry(PHATrackerEntry& entry)
{
    try {
        entry.state.set_state(PHATrackerEntryState::Blocked);
        ++entry.blocker_count;
        TRACE(entry, "Adding blocker to entry: {}, total: {}", entry.id, entry.blocker_count);
    } catch (tracker_entry_state_transition_error& exc) {
        throw tracker_entry_state_transition_error{fmt::format("{} (txid={})", exc.what(), entry.txid().value)};
    }
}

void PHATracker::unblock_entry(PHATrackerEntry& entry)
{
    // Default to Waiting, but we might actually be ready. If so, ready_to_schedule will move us to that state.
    AMPERE_ASSERT(entry.blocker_count > 0, "PHA Tracker Entry unblocked more times than blocked");
    --entry.blocker_count;
    TRACE(entry, "Resolving blocker on entry: {}, remaining: {}", entry.id, entry.blocker_count);
    if (entry.blocker_count == 0) {
        entry.state.set_state(PHATrackerEntryState::Waiting);

        if (entry.ready_to_bid())
            ready_to_schedule(entry);
    }
}

std::optional<PHATrackerEntry*> PHATracker::snp_in_list(PHATrackerEntry& entry)
{
    PHATrackerEntry* temp = &entry;
    while (temp->wake) {
        if (temp->snoop) // if snoop found
            return temp;
        else
            temp = temp->wake;
    }
    if (temp->snoop)
        return temp;

    return {};
}

void PHATracker::sfv_alloc(PHATrackerEntry& entry, PhysAddr addr)
{
    auto head = active_lines.find(addr.line_address());
    if (head != active_lines.end()) {
        PHATrackerEntry* head_entry = head->second;
        PHATrackerEntry* tail_entry = head->second;
        while (tail_entry->wake)
            tail_entry = tail_entry->wake;

        std::optional<PHATrackerEntry*> snp_entry = snp_in_list(*head_entry);

        if (pha && pha->trace_by_addr.contains(addr.value()))
            entry.trace = true;

        if (!snp_entry.has_value()) {
            if (head_entry->pipelineDone == 0) { // no snoop in the list and head is still in pipeline
                entry.wake = head_entry;
                if (head_entry->state.is_ready())
                    remove_from_ready_list(*head_entry);
                block_entry(*head_entry);
                if (active_lines[addr.line_address()] == head_entry)
                    active_lines[addr.line_address()] = &entry;
                TRACE(entry, "Entry {}, SFV allocated in state={}, Case 1", entry.id, entry.state.get_state());
                TRACE(*head_entry, "Entry {}, blocked by Entry {}", head_entry->id, entry.id);
            } else { // no snoop in the list and head is done in pipeline
                entry.wake = head_entry->wake;
                head_entry->wake = &entry;
                block_entry(entry);
                TRACE(entry, "Entry {}, SFV allocated in state={}, Case 2", entry.id, entry.state.get_state());
                TRACE(*head_entry, "Entry {}, now wakes up Entry={}", head_entry->id, entry.id);
            }
        } else {
            auto& snp = snp_entry.value();
            if (head_entry->snoop) { // if head is snoop
                AMPERE_ASSERT(head_entry == snp,
                              "Head is a snoop transaction but does not match snp_entry found in hazard list");
                entry.wake = head_entry->wake;
                head_entry->wake = &entry;
                block_entry(entry);
                TRACE(entry, "Entry {}, SFV allocated in state={}, Case 3", entry.id, entry.state.get_state());
            } else {
                if (head_entry->pipelineDone == 0) { // if head is not snoop and head is still in pipeline
                    // HEAD->SNP->Rest before the insertion
                    head_entry->wake = snp->wake; // HEAD->Rest
                    snp->wake = &entry;           // SNP->SFV
                    block_entry(entry);
                    entry.wake = head_entry; // SNP->SFV->HEAD->rest
                    if (head_entry->state.is_ready())
                        remove_from_ready_list(*head_entry);
                    block_entry(*head_entry);
                    if (active_lines[addr.line_address()] == head_entry)
                        active_lines[addr.line_address()] = snp;
                    ready_to_schedule(*snp); // this is a special case as the snp_entry was initially in blocked state
                    AMPERE_ASSERT(snp->state.get_state() == PHATrackerEntryState::Ready,
                                  "SNP was woken up after SFV insertion in hazard list");
                    TRACE(entry, "Entry {}, SFV allocated in state={}, Case 4", entry.id, entry.state.get_state());
                    TRACE(*head_entry, "Entry {}, blocked by Entry {}", head_entry->id, entry.id);
                } else { // if head is not snoop and head is done in pipeline
                    // HEAD->SNP->Rest before the insertion
                    entry.wake = snp->wake; // SFV->Rest
                    snp->wake = &entry;     // SNP->SFV
                    block_entry(entry);
                    head_entry->wake = snp; // HEAD->SNP->SFV->rest
                    TRACE(entry, "Entry {}, SFV allocated in state={}, Case 5", entry.id, entry.state.get_state());
                }
            }
        }
    } else {
        // No linked list present before SFV
        TRACE(entry, "Entry {}, SFV alloc new active line entry", entry.id);
        active_lines[addr.line_address()] = &entry;
    }
}

PHATrackerEntry* PHATracker::allocate(MeshSnp snp, pha_fsm::PHA_t pha_type, uint8_t entry_type)
{
    auto& entry = get_free_tracker_entry();
    init_from_snp(entry, snp);
    init_common(entry, pha_type, entry_type);

    // HACK: We map the incoming snp onto a Request object(Snp) to prevent segfaults of tracing.
    if (snp->Opcode == SnoopOpcode::SnpCleanInvalid) {
        entry.request = make<MeshReq>(entry.snoop->SrcID,
                                      entry.snoop->TgtID,
                                      entry.snoop->TxnID,
                                      RequestOpcode::SnpCleanInvalid,
                                      entry.snoop->Addr);
    } else if (snp->Opcode == SnoopOpcode::SnpMakeInvalid) {
        entry.request = make<MeshReq>(entry.snoop->SrcID,
                                      entry.snoop->TgtID,
                                      entry.snoop->TxnID,
                                      RequestOpcode::SnpMakeInvalid,
                                      entry.snoop->Addr);
    } else if (snp->Opcode == SnoopOpcode::SnpUniqueStash) {
        entry.request = make<MeshReq>(entry.snoop->SrcID,
                                      entry.snoop->TgtID,
                                      entry.snoop->TxnID,
                                      RequestOpcode::SnpUniqueStash,
                                      entry.snoop->Addr);
    } else if (snp->Opcode == SnoopOpcode::SnpUnique) {
        entry.request = make<MeshReq>(
            entry.snoop->SrcID, entry.snoop->TgtID, entry.snoop->TxnID, RequestOpcode::SnpUnique, entry.snoop->Addr);
    } else if (snp->Opcode == SnoopOpcode::SnpUniqueFwd) {
        entry.request = make<MeshReq>(
            entry.snoop->SrcID, entry.snoop->TgtID, entry.snoop->TxnID, RequestOpcode::SnpCleanFwd, entry.snoop->Addr);
    } else if (snp->Opcode == SnoopOpcode::SnpOnceFwd) {
        entry.request = make<MeshReq>(
            entry.snoop->SrcID, entry.snoop->TgtID, entry.snoop->TxnID, RequestOpcode::SnpOnceFwd, entry.snoop->Addr);
    } else if (snp->Opcode == SnoopOpcode::SnpCleanFwd) {
        entry.request = make<MeshReq>(
            entry.snoop->SrcID, entry.snoop->TgtID, entry.snoop->TxnID, RequestOpcode::SnpCleanFwd, entry.snoop->Addr);
    } else if (snp->Opcode == SnoopOpcode::SnpOnce) {
        entry.request = make<MeshReq>(
            entry.snoop->SrcID, entry.snoop->TgtID, entry.snoop->TxnID, RequestOpcode::SnpOnce, entry.snoop->Addr);
    } else {
        AMPERE_FATAL("New Snp flow not handled");
    }

    block_if_active(entry, snp->Addr, true);

    return &entry;
}

PHATrackerEntry& PHATracker::get_free_tracker_entry()
{
    AMPERE_ASSERT(!full(), "cannot allocate into a full PhaTracker");

    /* Grab next free tracker entry and populate it in the PHA Table */
    int n = free_list.front();
    free_list.pop_front();

    PHATrackerEntry& entry = entries[n];
    entry.age = tracker_age++;
    entry.state.set_state(PHATrackerEntryState::Allocated);
    return entry;
}

void PHATracker::block_if_active(PHATrackerEntry& entry, PhysAddr addr, bool skip_ahead)
{
    /* Determine if there is already an active cacheline that is operating on
     * this address.  If so we block this request and wake it once the active line
     * is handled */
    auto other = active_lines.find(addr.line_address());

    if (other != active_lines.end()) {
        PHATrackerEntry* other_entry = other->second;

        if (pha && pha->trace_by_addr.contains(addr.value()))
            entry.trace = true;

        block_entry(entry);

        // Find the tail of the list, unless we're skipping to the head of the line.
        if (!skip_ahead) {
            while (other_entry->wake)
                other_entry = other_entry->wake;
        }

        // We always insert *after* `other_entry`, which will either be the end of the list
        // (skip_ahead == false), or second in line (skip_ahead == true). It's possible that
        // `pup_check()` will promote a snoop even further (to the head), interrupting the
        // currently executing request.
        //
        entry.wake = other_entry->wake;
        other_entry->wake = &entry;

        pup_check(*other_entry);
    } else {
        active_lines[addr.line_address()] = &entry;
    }
}

void save_intr_snp_fields(PHATrackerEntry* interrupting_snp, PHATrackerEntry& interrupted_req)
{
    interrupting_snp->SnpIntrReq = true;
    interrupting_snp->input.SnpIntrReq = true;

    // Copy Snoop State from Interrupted Req
    interrupting_snp->snoop_status = interrupted_req.snoop_status;
    interrupting_snp->next_snoop_status = interrupted_req.next_snoop_status;
    interrupting_snp->snoop_address = interrupted_req.snoop_address;
    interrupting_snp->input.Gt1Copy = interrupting_snp->snoop_status.count() > 0;
    // Copy Cache State from Interrupted Req
    interrupting_snp->input.CacheState = interrupted_req.input.CacheState;
    // Copy PDB state from Interrupted Req
    interrupting_snp->pdb_state = interrupted_req.pdb_state;
    // Copy PO state from Interrupted Req
    interrupting_snp->input.PO = interrupted_req.input.PO;
    // Update SfHit from Interrupted Req
    interrupting_snp->input.SfHit =
        (interrupted_req.pdb_state != chi::FinalState::I) || (interrupted_req.input.CacheState != chi::FinalState::I);

    // Update the SfCmd to SF_LU if the Interrupting Snoop is a SnoopUniqueStash and FIXME : not in MMC mode.
    interrupting_snp->input.SfCmd = interrupting_snp->snoop->Opcode == chi::SnoopOpcode::SnpUniqueStash
                                        ? pha_fsm::SfCmd_t::SF_LU
                                        : interrupting_snp->input.SfCmd;

    interrupting_snp->input.IntrPdbDirty = interrupted_req.pdb_is_dirty();
    interrupting_snp->input.IntrPdbValid = interrupted_req.input.PdbValid;
}

/* The PUP (PHAT Update Pipeline) is not something we explicitly model, but there are
 * some things we might need to do when the state of an entry changes. In particular,
 * we might need to preempt an in-flight transaction with a snoop under the right
 * conditions.
 */
void PHATracker::pup_check(PHATrackerEntry& entry)
{
    bool awaiting_data = entry.request && is_copyback(entry.request->Opcode) && entry.wait4PdbData;

    if (entry.wake &&                   // we have something to wake up
        entry.wake->snoop &&            // that thing is a snoop
        ((entry.request_outstanding &&  //   we have a request (to the PmHA) outstanding
          !entry.wait4SnpCnt0 &&        //   we're not waiting for any snoop responses
          !entry.wait4CompAck &&        //   we're not waiting for a CompAck
          !awaiting_data)               //   we're not waiting for any data
         ||                             // or...
         (entry.state.is_allocated()))) //   we're brand new and haven't done anything yet
    {
        // We need to switch places with the entry we were going to wake up.
        TRACE(entry,
              "Entry {} is being preepmted by Entry {}: txid={}, {}, {}",
              entry.id,
              entry.wake->id,
              entry.wake->txid().value,
              entry.wake->request->Opcode,
              entry.wake->request->Addr);
        TRACE(*entry.wake, "Entry {} is preempting Entry {}", entry.wake->id, entry.id);

        if (entry.state.is_ready())
            remove_from_ready_list(entry);

        block_entry(entry);
        // entry.pdb_state = FinalState::I; // Not sure when/where this is actually done, but it is done
        // entry.input.PdToPha = false;

        auto previous_wake = entry.wake;
        entry.wake = previous_wake->wake;
        previous_wake->wake = &entry;

        save_intr_snp_fields(previous_wake, entry);

        if (active_lines[previous_wake->snoop->Addr.line_address()] == &entry)
            active_lines[previous_wake->snoop->Addr.line_address()] = previous_wake;

        TRACE(*previous_wake,
              "Interrupting Snoop {} CacheState {} pdb_state {} SfHit {} IntrPdbDirty {} IntrPdbValid {}",
              previous_wake->id,
              previous_wake->input.CacheState,
              previous_wake->pdb_state,
              previous_wake->input.SfHit,
              previous_wake->input.IntrPdbDirty,
              previous_wake->input.IntrPdbValid);

        ready_to_schedule(*previous_wake);
    } else if (entry.wake && entry.wake->snoop) {
        TRACE(entry,
              "Entry {} did not get preempted; reqout={} wait4SnpCnt0={} wait4CompAck={} awaiting_data={}",
              entry.id,
              entry.request_outstanding,
              entry.wait4SnpCnt0,
              entry.wait4CompAck,
              awaiting_data);
    }
}

void PHATracker::remove_from_ready_list(PHATrackerEntry& entry)
{
    auto it = ready_list.find(&entry);
    if (it != ready_list.end()) {
        it = ready_list.erase(it);
        return;
    }

    auto plat_it = ready_plat_list.find(&entry);
    if (plat_it != ready_plat_list.end()) {
        plat_it = ready_plat_list.erase(plat_it);
        return;
    }

    // Since `remove_from_ready_list` is being used for entries that get pre-empted by SnpCleanInvalid,
    // I think they should be "normal" entries, and not other snoops or victims. So we don't
    // expect them to be on either of the other two ready lists.
    //
    AMPERE_FATAL("expecting entry {} (txid={}) to be on ready_list or ready_plat_list", entry.id, entry.txid().value);
}

void PHATracker::init_common(PHATrackerEntry& entry, pha_fsm::PHA_t pha_type, uint8_t entry_type)
{
    entry.entry_type = entry_type;

    /* Static Inputs */
    entry.input.PHA = pha_type;
    entry.input.SfVictim = false;
    entry.input.L4Cache = false; // TODO

    /* Dynamic inputs */
    entry.input.PipelinePass = 0;
    entry.input.SfCmd = pha_fsm::SfCmd_initialize(entry.input.Opcode, (bool)entry.input.SfVictim);

    temp_set_defaults(entry);
}

void PHATracker::init_from_req(PHATrackerEntry& entry, const MeshReq& req, pha_fsm::PHA_t pha_type)
{
    entry.request = req;
    entry.input.FlowType = pha_fsm::FlowType_t::FLOW_REQ;
    entry.input.Opcode = req->Opcode;
    entry.input.Order = req->Order;
    entry.input.CopyBackL2L3 = req->RSVDC.CopyBackL2L3();
    entry.input.LikelyShared = req->LikelyShared;
    entry.input.ExpCompAck = req->ExpCompAck;
    entry.input.CF_SnpAttr_DoDWT = req->SnpAttr;
    entry.input.Excl = req->Excl;
    entry.input.UseDct = !disable_dct && !entry.input.Excl
                         && (chi::is_alloc_read(req->Opcode) || req->Opcode == chi::RequestOpcode::ReadOnce);

    if (pha_type == pha_fsm::PHA_t::Pmha) {
        entry.input.UseDmt =
            !disable_dmt && !entry.input.Excl && !entry.input.LikelyShared
            && (chi::is_alloc_read(req->Opcode)
                || (chi::is_read(req->Opcode) && (req->Order == chi::RequestOrder::NoOrder || entry.input.ExpCompAck)))
            && dmt_die_disable->count(req->ReturnNID.die_id()) == 0;
        entry.input.UseDwt =
            !disable_dwt && !entry.input.Excl && !entry.input.ExpCompAck && chi::is_imm_write(req->Opcode);
    }
}

void PHATracker::init_from_snp(PHATrackerEntry& entry, const MeshSnp& snp)
{
    entry.snoop = snp;
    entry.input.FlowType = pha_fsm::FlowType_t::FLOW_SNP;
    entry.input.Opcode = static_cast<chi::RequestOpcode>(static_cast<int>(snp->Opcode) + 0x90);
    entry.input.LikelyShared = false;
    entry.input.ExpCompAck = false;
    entry.input.CF_SnpAttr_DoDWT = false;
    entry.input.Excl = false;
    entry.input.UseDct = false;
    entry.input.UseDmt = false;
    entry.input.UseDwt = false;
}

std::pair<bool, uint8_t> PHATracker::deallocate_run()
{
    /* One entry can be deallocated per call(cycle).
     * Deallocation is actually done in a side pipeline.  The main pipeline may
     * be done with this tracker entry, yet sometimes we have to wait for chi
     * transactions to complete before we deallocate
     *
     * TODO: Freeing is done in a round roubin fashion, not sure how this compares
     * to what I have done here
     *
     * Entries that depend on this transaction are woken up */
    for (auto it = dealloc_list.begin(); it != dealloc_list.end(); it++) {
        auto entry = *it;

        TRACE(*entry, "Entry {} is being deallocated", entry->id);

        auto addr = entry->request ? entry->request->Addr : entry->snoop->Addr;
        auto active_entry_it = active_lines.find(addr.line_address());

        if (active_entry_it != active_lines.end() && active_entry_it->second == entry)
            active_lines.erase(active_entry_it);

        /* Allow any other requests that need to operate on this cacheline */
        if (entry->wake) {
            auto waking_entry = entry->wake;
            active_lines[addr.line_address()] = waking_entry;

            unblock_entry(*waking_entry);

            entry->wake = nullptr;
            TRACE(*entry,
                  "Entry {} woke up waiting entry {}, which is now in state {}, address is {}",
                  entry->id,
                  waking_entry->id,
                  waking_entry->state.get_state(),
                  waking_entry->request->Addr);
        }

        if (entry->sfvwake) {
            auto waking_entry = entry->sfvwake;

            unblock_entry(*waking_entry);

            entry->sfvwake = nullptr;
            TRACE(*entry,
                  "Entry {} woke up waiting entry {}, which is now in state {}, address is {}",
                  entry->id,
                  waking_entry->id,
                  waking_entry->state.get_state(),
                  waking_entry->request->Addr);
        }

        uint8_t pcrd_type = entry->entry_type;

        /* Free PHA entry */
        free_list.push_back(entry->id);
        it = dealloc_list.erase(it);
        entry->reset();

        /* Wake up all entries that need to generate SFVs */
        for (auto itr = sfv_blocked.begin(); itr != sfv_blocked.end(); itr++) {
            ready_to_schedule(**itr);
            TRACE(*entry,
                  "Entry {} is waking entry {} (now in state {}) which needs to allocate an SFV",
                  entry->id,
                  (*itr)->id,
                  (*itr)->state.get_state());
        }
        sfv_blocked.clear();

        return {true, pcrd_type};
    }

    return {false, 0};
}

void PHATracker::temp_set_defaults(PHATrackerEntry& entry)
{
    /* TODO TODO
     * The following should be set by the requesters but may not be.  The following
     * fields are required to be set to this value for this opcode as stated by the
     * chi spec
     */
    switch (entry.input.Opcode) {
    case chi::RequestOpcode::CleanInvalid:
        entry.input.Order = chi::RequestOrder::NoOrder;
        entry.input.LikelyShared = false;
        entry.input.ExpCompAck = false;
        entry.input.Excl = false;
        break;
    case chi::RequestOpcode::ReadNoSnp:
        entry.input.LikelyShared = false;
        entry.input.CF_SnpAttr_DoDWT = false;
        break;
    case chi::RequestOpcode::ReadOnce:
        entry.input.LikelyShared = false;
        entry.input.CF_SnpAttr_DoDWT = true;
        entry.input.Excl = false;
        if (entry.request->Order != RequestOrder::NoOrder)
            entry.input.ExpCompAck = true;
        break;
    case chi::RequestOpcode::ReadClean:
    case chi::RequestOpcode::ReadNotSharedDirty:
        entry.input.Order = chi::RequestOrder::NoOrder;
        entry.input.ExpCompAck = true;
        entry.input.CF_SnpAttr_DoDWT = true;
        break;
    case chi::RequestOpcode::ReadShared:
        entry.input.Order = chi::RequestOrder::NoOrder;
        entry.input.ExpCompAck = true;
        entry.input.CF_SnpAttr_DoDWT = true;
        break;
    case chi::RequestOpcode::ReadUnique:
        entry.input.Order = chi::RequestOrder::NoOrder;
        entry.input.ExpCompAck = true;
        break;
    case chi::RequestOpcode::WriteNoSnpPtl:
        if (entry.request->Order != RequestOrder::NoOrder)
            entry.input.ExpCompAck = true;
    case chi::RequestOpcode::WriteNoSnpFull:
        entry.input.LikelyShared = false;
        entry.input.CF_SnpAttr_DoDWT = false;
        break;
    case chi::RequestOpcode::WriteBackFull:
        entry.input.Order = chi::RequestOrder::NoOrder, entry.input.ExpCompAck = false;
        entry.input.CF_SnpAttr_DoDWT = true;
        entry.input.Excl = false;
        break;
    case chi::RequestOpcode::WriteEvictOrEvict:
        entry.input.Order = chi::RequestOrder::NoOrder;
        entry.input.ExpCompAck = true;
        entry.input.CF_SnpAttr_DoDWT = true;
        entry.input.Excl = false;
        break;
    case chi::RequestOpcode::WriteCleanFull:
    case chi::RequestOpcode::WriteEvictFull:
        entry.input.Order = chi::RequestOrder::NoOrder;
        entry.input.ExpCompAck = false;
        entry.input.CF_SnpAttr_DoDWT = true;
        entry.input.Excl = false;
        break;
    case chi::RequestOpcode::Evict:
        entry.input.Order = chi::RequestOrder::NoOrder;
        entry.input.LikelyShared = false;
        entry.input.ExpCompAck = false;
        entry.input.CF_SnpAttr_DoDWT = true;
        entry.input.Excl = false;
        break;
    case chi::RequestOpcode::MakeUnique:
    case chi::RequestOpcode::CleanUnique:
        entry.input.Order = chi::RequestOrder::NoOrder;
        entry.input.LikelyShared = false;
        entry.input.ExpCompAck = true;
        entry.input.Excl = false;
        break;
    case chi::RequestOpcode::SnpCleanInvalid:
    case chi::RequestOpcode::SnpMakeInvalid:
    case chi::RequestOpcode::SnpUniqueStash:
    case chi::RequestOpcode::SnpUnique:
    case chi::RequestOpcode::SnpUniqueFwd:
    case chi::RequestOpcode::SnpOnceFwd:
    case chi::RequestOpcode::SnpCleanFwd:
    case chi::RequestOpcode::SnpOnce:
        /* We don't care about defaults */
        break;
    case chi::RequestOpcode::WriteUniqueFull:
    case chi::RequestOpcode::WriteUniquePtl:
        entry.input.CF_SnpAttr_DoDWT = true;
        entry.input.Excl = false;
        entry.input.ExpCompAck = true;
        break;
    default:
        AMPERE_FATAL("Unhandled default Chi spec states not set for opcode: {}", entry.input.Opcode);
        break;
    };
}

void PHATracker::dump(std::ostream& os) const
{
    std::set<uint64_t> on_sfv_blocked_list;

    for (auto& sfv_entry : sfv_blocked)
        on_sfv_blocked_list.insert(sfv_entry->id);

    os << std::endl
       << name() << ":" << std::endl
       << "Flags Order = PipelineDone, wait4DBID, wait4Comp, wait4PdbData, wait4AdpData, wait4CompAck, wait4SnpCnt, "
          "blocked"
       << std::endl
       << std::endl
       << " ID |        Addr        |        ReqOp       |  Age  |    State     |      Flags      | Pass | FSM S# | "
          "SrcID | TxnID |  txid | Wake ID | SFV Wake |\n";

    for (auto& entry : entries) {
        bool request = false;

        if (entry.request != nullptr)
            request = true;
        else if (entry.snoop != nullptr)
            request = false;
        else
            continue;

        auto src_id = request ? entry.request->SrcID : entry.snoop->SrcID;

        std::string wake_id;

        if (entry.wake)
            wake_id = std::to_string(entry.wake->id);
        else if (on_sfv_blocked_list.contains(entry.id))
            wake_id = "SFV";
        else
            wake_id = "";

        fmt::print(os,
                   "{:3d} | 0x{:016x} | {:18} | {:5d} | {:12} | {},{},{},{},{},{},{},{} | {:4d} | {:6d} |  {}.{:02} | "
                   "{:5} | {:5} | {:7} | {:8} |\n",
                   entry.id,
                   request ? entry.request->Addr.value() : entry.snoop->Addr.value(),
                   entry.input.Opcode,
                   entry.age,
                   entry.state.get_state(),
                   entry.pipelineDone ? "T" : "F",
                   entry.wait4DBID ? "T" : "F",
                   entry.wait4Comp ? "T" : "F",
                   entry.wait4PdbData ? "T" : "F",
                   entry.wait4AdbData ? "T" : "F",
                   entry.wait4CompAck ? "T" : "F",
                   entry.wait4SnpCnt0 ? "T" : "F",
                   entry.state.get_state() == PHATrackerEntryState::Blocked ? "T" : "F",
                   entry.input.PipelinePass,
                   entry.output.TotalSNo,
                   src_id.die_id(),
                   src_id.agent_id(),
                   request ? entry.request->TxnID : 0,
                   request ? entry.request->txid().value : entry.snoop->txid().value,
                   wake_id,
                   entry.sfvwake ? std::to_string(entry.sfvwake->id) : "");
    }
}

void PHATrackerEntryState::set_state(State new_state)
{
    switch (new_state) {
    case Invalid:
        AMPERE_ASSERT(state == Deallocating || state == Invalid, "illegal transition from {} -> {}", state, new_state);
        break;
    case Allocated:
        AMPERE_ASSERT(state == Invalid, "illegal transition from {} -> {}", state, new_state);
        break;
    case Blocked:
        if (!(state == Allocated || state == Executing || state == Waiting || state == Blocked || state == Ready))
            throw tracker_entry_state_transition_error{
                fmt::format("illegal transition from {} -> {}", state, new_state)};
        AMPERE_ASSERT(
            state == Allocated || state == Executing || state == Waiting || state == Blocked || state == Ready,
            "illegal transition from {} -> {}",
            state,
            new_state);
        break;
    case Waiting:
        AMPERE_ASSERT(state == Executing || state == Blocked, "illegal transition from {} -> {}", state, new_state);
        break;
    case Ready:
        AMPERE_ASSERT(state == Allocated || state == Blocked || state == Waiting,
                      "illegal transition from {} -> {}",
                      state,
                      new_state);
        break;
    case Executing:
        AMPERE_ASSERT(state == Ready, "illegal transition from {} -> {}", state, new_state);
        break;
    case Deallocating:
        AMPERE_ASSERT(state == Waiting, "illegal transition from {} -> {}", state, new_state);
        break;
    }

    state = new_state;
}

} // namespace ampere::mesh
