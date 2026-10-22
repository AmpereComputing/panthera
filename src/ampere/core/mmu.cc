//
// Copyright (c) 2018-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/mmu.h"

#include <algorithm>
#include <optional>
#include <ranges>
#include <tuple>
#include <utility>
#include "ampere/archx/translation.h"
#include "ampere/core/ixu_defs.h"
#include "ampere/core/mmu/otb.h"
#include "ampere/core/mmu/req.h"
#include "ampere/sc/transaction.h"
#include "ampere/uarch_common/memop.h"

namespace ampere::core {

using namespace uarch_common;

MMU::MMU(const sc_core::sc_module_name &name) : Module(name), otb{"otb"}
{
    connect(tlb_lookup).name("tlb_lookup_pipe").latency(lookup_pipe_latency);
    connect(s1tpc_lookup).name("s1tpc_lookup_pipe").latency(s1tpc_pipe_latency);
    connect(table_walk).name("table_walk_pipe").latency(walk_pipe_latency);

    tlb.driver.bind(driver_port);

    otb.available.bind(mmu_can_accept);
    get_clock()->add_zdelay_connection(&otb, this);

    if (allocation_fairness) {
        requests.resize(otb.non_shared_sections());
        auto sections = std::views::iota(0U, otb.non_shared_sections());
        allocation_priority = {sections.begin(), sections.end()};
    }
}

MMU::MMU(const sc_core::sc_module_name &name, unsigned load_pipes, unsigned sta_pipes) : MMU(name)
{
    AMPERE_ASSERT(!otb.is_sectioned() || (otb.lsu_sections() >= load_pipes && otb.lsu_sections() >= sta_pipes),
                  "LSU sections must be greater or equal to both load and STA pipes");
}

bool MMU::run()
{
    accept_requests(inst_mmu_request, inst_mmu_ack);
    accept_requests(data_mmu_request, data_mmu_ack);
    process_requests();
    accept_invalidate_requests();
    accept_pte_responses();

    if (flush_port->can_read())
        handle_flush();

    if (invalidation_executing) {
        do_invalidation();
    } else {
        do_s1tpc_lookups();
        do_tlb_lookups();
        do_walks();
    }
    if (!tlb_invalidations.empty()) {
        schedule_tlb_invalidations();
    } else if (!invalidation_executing && get_cycle() > invalidation_block) {
        // No new lookups or table walks are scheduled while an invalidation is
        // pending to allow the pipelines to clear (invalidations need
        // exclusive access to the TLB, S1-TPC, and S2-TPC)
        schedule_tlb_lookups();
        schedule_walks();
    }
    send_responses(inst_response_queue, inst_mmu_response);
    send_responses(data_response_queue, data_mmu_response);

    mmu_busy_event();
    collect_pmu_data();

    return !otb.empty() || !inst_response_queue.empty() || !data_response_queue.empty() || !tlb_invalidations.empty()
           || invalidation_executing || otb.locked_out();
}

/* Initialize the WalkFSM and return true iff the table walk is already
 * complete based on information found in the S1TPC */
void MMU::init_walk_fsm(OTBEntry &otbentry)
{
    auto &entry = otbentry.pwt;
    auto req = otbentry.request;

    auto el = driver_port->exception_level();
    if (el != EL1 && el != EL0) { /* Only implement EL0 or EL1 translations */
        throw std::runtime_error("MMU only expects EL0 or EL1");
    }

    // Even though these are the default values, set them here because walks
    // can be re-started
    entry.done = false;
    entry.starting_level_fetched = false;

    /* Determine the granule and input/output address sizes */
    auto tr = translation(otbentry);
    entry.virt_addr = req->virt_addr;
    entry.granule_size = tr.granule_size;
    entry.input_size = tr.input_size;
    entry.output_size = tr.output_size;

    /* Note: starting_level() must be called after other initialization of
     * PWTEntry, since it depends upon its variables being correct */
    entry.level = entry.starting_level();

    /* See if we can short-circuit any lookups using the S1TPC */
    auto s1tpc_hit = s1tpc.lookup(otbentry.request, TPCTag{entry, otbentry.asid, otbentry.using_ttbr0()});
    ev_pmu(otbentry.request,
           ev_num(otbentry.request->is_instruction ? archx::PMU::MMU_I_S1_WALK_CACHE_LOOKUP
                                                   : archx::PMU::MMU_D_S1_WALK_CACHE_LOOKUP));

    if (s1tpc_hit) {
        /* Loop until we've copied over the PTE addresses and memory contents
         * for all levels of the table walk for which the cached TPC entry was
         * valid for that level AND the indices into that level of the page
         * table match. */
        for (uint8_t l = entry.level; (*s1tpc_hit).index(l) == entry.index(l) && l <= (*s1tpc_hit).level && l < 3; l++)
        {
            entry.pte_addr[l] = (*s1tpc_hit).pte_addr[l];
            entry.pte[l] = (*s1tpc_hit).pte[l];
            entry.level = l;
        }
        entry.starting_level_fetched = true;

        if (otbentry.request->is_instruction) {
            if (s1tpc_hit->level == 0)
                ev_pmu(otbentry.request, ev_num(archx::PMU::MMU_I_TRANS_CACHE_HIT_S1L0_WALK));
            else if (s1tpc_hit->level == 1)
                ev_pmu(otbentry.request, ev_num(archx::PMU::MMU_I_TRANS_CACHE_HIT_S1L1_WALK));
            else
                ev_pmu(otbentry.request, ev_num(archx::PMU::MMU_I_TRANS_CACHE_HIT_S1L2_WALK));
        } else {
            if (s1tpc_hit->level == 0)
                ev_pmu(otbentry.request, ev_num(archx::PMU::MMU_D_TRANS_CACHE_HIT_S1L0_WALK));
            else if (s1tpc_hit->level == 1)
                ev_pmu(otbentry.request, ev_num(archx::PMU::MMU_D_TRANS_CACHE_HIT_S1L1_WALK));
            else
                ev_pmu(otbentry.request, ev_num(archx::PMU::MMU_D_TRANS_CACHE_HIT_S1L2_WALK));
        }
    } else {
        /* Calculate the base address for the top-level table from TTBR */
        uint64_t baseaddress;
        uint64_t baselowerbound = 3 + entry.input_size - ((3 - entry.level) * entry.stride() + entry.granule_size);
        if (entry.output_size == 52) {
            uint8_t z = (baselowerbound < 6) ? 6 : baselowerbound;
            baseaddress = tr.ttbr.extract_range(5, 2) << 47 | tr.ttbr.extract_range(47, z) << z;
        } else {
            baseaddress = tr.ttbr.extract_range(47, baselowerbound) << baselowerbound;
        }

        /* Calculate the index into the table from the correct bits in the VA */
        entry.pte_addr[entry.level] = PhysAddr{baseaddress | (entry.index(entry.level) << 3)};

        /* Get contents of the first descriptor */
        entry.pte[entry.level] = driver_port->read_memory(entry.pte_addr[entry.level]);
    }
}

/* Return whether the walk is done after the current stage, and set `done =
 * true` if so. */
bool MMU::check_walk_done(OTBEntry &otbentry)
{
    auto &entry = otbentry.pwt;
    Bitfield descriptor(entry.pte[entry.level]);

    /* Detect invalid/empty descriptors */
    if (!descriptor[0] || (entry.level == 3 && descriptor.extract_range(1, 0) == 0b10)) {
        otbentry.faulted = true;
        entry.done = true;
        return true; // walk done
    }

    if (descriptor.extract_range(1, 0) == 0b01 || entry.level == 3) {
        /* This is a block or page descriptor, so we are done walking */
        entry.done = true;

        /* Fault if software is managing access flags and this descriptor's AF
         * bit is not set*/
        Bitfield tcr_el1 = Bitfield{driver_port->read_sysreg(TCR_EL1)};
        if (!descriptor[10] /* AF bit */
            && !tcr_el1[39] /* TCR_EL1.HA */)
        {
            otbentry.faulted = true;
            return true;
        }

        /* Now, calculate the output address */
        uint8_t descbottom = (3 - entry.level) * entry.stride() + entry.granule_size;
        otbentry.phys_addr = PhysAddr{descriptor.extract_range(47, descbottom) << descbottom
                                      | entry.virt_addr.extract_range(descbottom - 1, 0)};
        otbentry.phys_addr += phyaddroffset;
        if (entry.output_size == 52) {
            otbentry.phys_addr |= descriptor.extract_range(15, 12) << 47;
            otbentry.phys_addr += phyaddroffset;
        }
        otbentry.page_size = PageSize(force_granule_pages ? entry.granule_size : descbottom);

        return true;
    }

    return false;
}

/* Returns whether the walk is done after the current stage */
bool MMU::do_walk_stage(OTBEntry &otbentry)
{
    auto &entry = otbentry.pwt;
    Bitfield descriptor(entry.pte[entry.level]);

    /*
     * Because the calculation for the first level's address occurs in
     * init_walk_fsm(), we don't want to advance the level of the page walk
     * we're on until after the first PTE has been fetched from the L2.
     */
    if (!entry.starting_level_fetched) {
        entry.starting_level_fetched = true;
        return false;
    }

    if (check_walk_done(otbentry))
        return true;

    /* If we got here, this descriptor points to another table. First, find the
     * base address for the next-level table */
    uint64_t baseaddress = descriptor.extract_range(47, entry.granule_size) << entry.granule_size;
    if (entry.output_size == 52) {
        baseaddress |= descriptor.extract_range(15, 12) << 47;
    }

    /* Get contents of descriptor for the next level of the walk */
    entry.level++;
    entry.pte_addr[entry.level] = PhysAddr{baseaddress | (entry.index(entry.level) << 3)};
    entry.pte[entry.level] = driver_port->read_memory(entry.pte_addr[entry.level]);

    return false;
}

void MMU::generate_response(OTBEntry &otbentry)
{
    if (otbentry.request->is_instruction)
        inst_response_queue.push_back(otbentry.otb_id);
    else
        data_response_queue.push_back(otbentry.otb_id);
}

void MMU::do_s1tpc_lookups()
{
    while (s1tpc_lookup.can_take()) {
        auto &otbentry = otb.at(s1tpc_lookup.take());

        otbentry.executing = false;
        otbentry.ready = true;

        init_walk_fsm(otbentry); // will perform S1TPC lookup
        ev_pmu(otbentry.request,
               ev_num(otbentry.request->is_instruction ? archx::PMU::ITLB_WALK : archx::PMU::DTLB_WALK));
        table_walk_init_event(otbentry.request, otb.event.otb_id(otbentry.otb_id), ev_level(otbentry.pwt.level));
        otbentry.op = MMUOp::PageWalk;
    }
}

void MMU::do_tlb_lookups()
{
    while (tlb_lookup.can_take()) {
        auto &otbentry = otb.at(tlb_lookup.take());

        // Should only happen when MRU is uninitialized because we haven't seen
        // a TLB hit yet
        if (!otbentry.lookup_size) {
            assert(s1tpc_lookup.can_put());
            s1tpc_lookup.put(otbentry.otb_id);
            continue;
        }

        auto hit = tlb.lookup(otbentry.request,
                              L2TLBTag(otbentry.request->virt_addr,
                                       otbentry.asid,
                                       *otbentry.lookup_size,
                                       otbentry.request->is_instruction,
                                       tlb.get_set_bits()));

        tlb_access_event(otbentry.request,
                         hit_data(static_cast<bool>(hit)),
                         otb.event.otb_id(otbentry.otb_id),
                         otb.event.page_bits(otbentry.lookup_size ? otbentry.lookup_size->page_bits() : 0));

        if (hit && (*otbentry.lookup_size == hit->page_size)) {
            select_mru(otbentry.request->is_instruction).set_mru(*otbentry.lookup_size);

            otbentry.executing = false;
            otbentry.ready = false;
            otbentry.phys_addr = hit->phys_addr;
            otbentry.page_size = hit->page_size;

            if (!otbentry.request->is_instruction)
                ev_pmu(otbentry.request,
                       ev_num(otbentry.request->is_store ? archx::PMU::L2D_TLB_WR : archx::PMU::L2D_TLB_RD));

            generate_response(otbentry);
        } else {
            if (otbentry.flushing) {
                if (!otbentry.deallocating) {
                    if (!otbentry.request->is_instruction)
                        ev_pmu(otbentry.request,
                               ev_num(otbentry.request->is_store ? archx::PMU::L2D_TLB_WR : archx::PMU::L2D_TLB_RD));
                    otb.deallocate_immediately(otbentry);
                }
                continue;
            }
            // If we have exhausted all seen page sizes, lookup the S1-TPC.
            // Otherwise, perform another TLB lookup with the next page size
            otbentry.lookup_size = otbentry.mru.next_size();
            if (!otbentry.lookup_size) {
                assert(s1tpc_lookup.can_put());
                s1tpc_lookup.put(otbentry.otb_id);
                if (!otbentry.request->is_instruction)
                    ev_pmu(otbentry.request,
                           ev_num(otbentry.request->is_store ? archx::PMU::L2D_TLB_WR : archx::PMU::L2D_TLB_RD));
            } else {
                otbentry.op = MMUOp::Recycle;
                otbentry.executing = false;
                otbentry.ready = true;
            }
        }
    }
}

void MMU::handle_flush()
{
    auto flush = flush_port->read();
    // Only flush MMU on non-speculative flushes
    if (!is_nonspeculative_flush(flush->type))
        return;

    flush_event();
    otb.flush(flush);
}

void MMU::do_invalidation()
{
    if (tlb_lookup.can_take()) {
        tlb_lookup.take();
        invalidation_executing = false;

        auto tlbi = tlb_invalidations.front();
        if (inst_tlbi_request_out.bind_count())
            inst_tlbi_request_out->put(tlbi);
        if (data_tlbi_request_out.bind_count())
            data_tlbi_request_out->put(tlbi);

        auto &ainst = tlbi->insn->archx_instruction;
        if (ainst->is_tlbi_by_va()) {
            auto victims =
                tlb.invalidate_by_va(ainst->tlbi_by_va_asid(), ainst->tlbi_by_va_ttl(), ainst->tlbi_by_va_addr());
            for (const auto &victim : victims) {
                select_mru(victim.is_instr).decrement_page_counter(victim.page_size);
            }

            // Last-level TLBIs don't affect the TPC. For all-level, just flush the whole thing
            // FIXME: this over-invalidates the TPC
            if (ainst->is_tlbi_by_va_all_level())
                s1tpc.clear();
        } else {
            // For everything that's not TLBI by VA, just flush everything
            // FIXME: this over-invalidates in many cases
            tlb.clear();
            s1tpc.clear();
            imru = MRU();
            dmru = MRU();

            // For any TLBI which might invalidate things from multiple sets, the MMU walks all the sets,
            // one per cycle. During that time it can't service any lookups.
            if (enable_invalidation_block)
                invalidation_block = get_cycle() + tlb.get_num_sets();
        }

        tlb_invalidations.pop();

        // Restart all in-progress translations
        auto new_asid = driver_port->asid();

        for (auto &[_, otb_entry] : otb) {
            auto tr = context(otb_entry.request);
            auto &mru = select_mru(otb_entry.request->is_instruction);
            otb_entry.reset(mru, {new_asid, tr});
        }
    }
}

void MMU::do_walks()
{
    while (table_walk.can_take()) {
        auto &otbentry = otb.at(table_walk.take());
        otbentry.fsm_credit->release();
        do_walk_stage(otbentry);
        if (otbentry.pwt.done) {
            otbentry.executing = false;
            otbentry.ready = false;

            if (!otbentry.faulted) {
                bool is_4k_page = (otbentry.page_size == PageSize::Size::PS_4KB);
                bool is_64k_page = (otbentry.page_size == PageSize::Size::PS_64KB);
                if (enable_contiguous_bit && (is_4k_page || is_64k_page)) {
                    Bitfield last_level_pte(otbentry.pwt.pte[3]);

                    // Bit 52 of the PTE is the contiguous hint, which says that this page is part of a larger group of
                    // physically contiguous pages and can be cached that way in the TLB. This can apply at higher
                    // page levels too, but our hardware only plans to support it at the last level.
                    if (last_level_pte[52] == 1) {
                        // A 4k PTE with the contig bit set produces a 64k translation, and a 64k PTE produces 2M.
                        otbentry.page_size = is_4k_page ? PageSize::Size::PS_64KB : PageSize::Size::PS_2MB;
                        contiguous_fill_event(otbentry.request,
                                              otb.event.otb_id(otbentry.otb_id),
                                              otb.event.page_bits(otbentry.page_bits()));
                    }
                }
                auto victim =
                    tlb.refill(otbentry.request,
                               L2TLBTag(otbentry.request->virt_addr,
                                        otbentry.asid,
                                        otbentry.page_size,
                                        otbentry.request->is_instruction,
                                        tlb.get_set_bits()),
                               L2TLBEntry(otbentry.phys_addr, otbentry.page_size, otbentry.request->is_instruction));

                auto &mru = select_mru(otbentry.request->is_instruction);
                mru.increment_page_counter(otbentry.page_size);
                mru.set_mru(otbentry.page_size);

                if (victim) {
                    select_mru(victim->is_instr).decrement_page_counter(victim->page_size);
                }
                s1tpc.refill(
                    otbentry.request, TPCTag{otbentry.pwt, otbentry.asid, otbentry.using_ttbr0()}, otbentry.pwt);

                if (!otbentry.request->is_instruction) {
                    ev_pmu(otbentry.request,
                           ev_num(otbentry.request->is_store ? archx::PMU::L2D_TLB_REFILL_WR
                                                             : archx::PMU::L2D_TLB_REFILL_RD));
                }

                ev_pmu(
                    otbentry.request,
                    ev_num(otbentry.request->is_instruction ? archx::PMU::L2I_TLB_REFILL : archx::PMU::L2D_TLB_REFILL));
                ev_pmu(otbentry.request,
                       ev_num(otbentry.request->is_instruction ? archx::PMU::MMU_I_S1_WALK_CACHE_REFILL
                                                               : archx::PMU::MMU_D_S1_WALK_CACHE_REFILL));

                switch (otbentry.page_size.value()) {
                case (PageSize::Size::PS_4KB):
                case (PageSize::Size::PS_64KB):
                    ev_pmu(otbentry.request,
                           ev_num(otbentry.request->is_instruction ? archx::PMU::ITLB_WALK_SMALL
                                                                   : archx::PMU::DTLB_WALK_SMALL));
                    break;
                case (PageSize::Size::PS_2MB):
                case (PageSize::Size::PS_512MB):
                case (PageSize::Size::PS_1GB):
                case (PageSize::Size::PS_4TB):
                    ev_pmu(otbentry.request,
                           ev_num(otbentry.request->is_instruction ? archx::PMU::ITLB_WALK_LARGE
                                                                   : archx::PMU::DTLB_WALK_LARGE));
                    break;
                default:
                    AMPERE_ERROR("MMU PMU Error: Unknown Page Size {}", otbentry.page_size.index());
                    break;
                }

            } else {
                if (otbentry.request->is_instruction)
                    ev_pmu(otbentry.request, ev_num(archx::PMU::MMU_I_S1_WALK_FAULT));
                else
                    ev_pmu(otbentry.request, ev_num(archx::PMU::MMU_D_S1_WALK_FAULT));
                fault_event(otbentry.request, otb.event.otb_id(otbentry.otb_id));
            }
            generate_response(otbentry);
        } else if (pte_fetch_out->can_put()) {
            otbentry.executing = false;
            otbentry.ready = false;
            auto mmureq = otbentry.request;
            auto pwt = otbentry.pwt;

            ev_pmu(
                otbentry.request,
                ev_num(otbentry.request->is_instruction ? archx::PMU::MMU_I_WALK_STEPS : archx::PMU::MMU_D_WALK_STEPS));
            ev_pmu(otbentry.request,
                   ev_num(otbentry.request->is_instruction ? archx::PMU::ITLB_STEP : archx::PMU::DTLB_STEP));

            pte_fetch_event(
                otbentry.request, otb.event.otb_id(otbentry.otb_id), pwt.pte_addr[pwt.level], ev_level(pwt.level));

            auto memreq = make_child<MemReq>(
                mmureq,
                otbentry.otb_id,
                pwt.pte_addr[pwt.level],
                mmureq->is_instruction && !unified_mmu ? MemOp::Requester::IMMU : MemOp::Requester::DMMU,
                MemOp::Type::READ,
                8,
                false,
                MemOp::PrefetchType::NONE,
                MemOp::BarrierType::BAR_NONE,
                MemOp::ATOMIC_NONE,
                MemOp::SNOOP_NONE,
                PageSize(otbentry.pwt.granule_size).value());
            pte_fetch_out->put(memreq);
        }
    }
}

void MMU::accept_pte_responses()
{
    while (pte_fetch_in->can_take()) {
        auto memresp = pte_fetch_in->take();

        // Ignore L2 responses for non-existent OTB entries. This is possible
        // after invalidates since we re-start translations, and don't have a
        // way of telling the L2 to throw out previous requests.
        auto *entry = otb.find(memresp->id);
        if (!entry)
            continue;

        auto &otbentry = *entry;
        auto &pwt = otbentry.pwt;

        // Ignore L2 response if we are not currently waiting on a PTE response from this PA
        if (otbentry.op != MMUOp::PageWalk || otbentry.ready || otbentry.executing || pwt.done
            || pwt.pte_addr[pwt.level] != memresp->phys_addr)
        {
            continue;
        }

        // Mark this entry as ready to be scheduled by the WalkFSM again
        otbentry.ready = true;
        pte_fetched_event(
            otbentry.request, otb.event.otb_id(otbentry.otb_id), pwt.pte_addr[pwt.level], ev_level(pwt.level));

        /*
         * Siryn MMU spec.: "Outstanding translations are terminated, and the
         * OTB entry cannot be reused until a possible outstanding PTE fetch
         * returns."
         *
         * We have kept this OTB entry around even though it was flushed, but
         * now that its outstanding PTE fetch has completed, it is time for it
         * to go.
         */
        if (otbentry.flushing && !otbentry.deallocating)
            otb.deallocate_immediately(otbentry);
    }
}

void MMU::accept_requests(InOpt<MMUReq> &mmu_request, OutOpt<MMUAck> &mmu_ack)
{
    /* Accept new lookup requests */
    while (mmu_request.bind_count() && mmu_request->can_take() && mmu_ack->can_put()) {
        auto mmureq = mmu_request->take();
        if (allocation_fairness) {
            auto &section = mmureq->section;
            section = (mmureq->is_instruction) ? otb.lsu_sections() : mmureq->channel % otb.lsu_sections();
            AMPERE_ASSERT(!requests[section], "Request already present in section");
            requests[section] = std::move(mmureq);
        } else {
            requests.emplace_back(std::move(mmureq));
        }
    }
}

void MMU::process_requests()
{
    if (!allocation_fairness) {
        for (auto &req : requests) {
            if (req)
                process_request(req, req->is_instruction ? inst_mmu_ack : data_mmu_ack);
        }
        requests.clear();
    } else {
        auto current_priority = std::move(allocation_priority);
        allocation_priority = {};
        allocation_priority.reserve(current_priority.size());
        auto front = allocation_priority.begin();
        for (auto section : current_priority) {
            if (auto &req = requests[section]; req) {
                if (process_request(req, req->is_instruction ? inst_mmu_ack : data_mmu_ack)) {
                    allocation_priority.emplace_back(section);
                    continue;
                }
            }
            front = ++allocation_priority.insert(front, section);
        }
        AMPERE_ASSERT(allocation_priority.size() == current_priority.size(), "Lost request");
        std::fill(requests.begin(), requests.end(), nullptr);
    }
}

bool MMU::process_request(const MMUReq &mmureq, OutOpt<MMUAck> &mmu_ack)
{
    /* If we recently flushed, do not allocate any new OTBs. The ICF/LSU
     * must re-request any translation requests made during this time
     * period */
    if (otb.locked_out()) {
        auto mmuack = make_child<MMUAck>(mmureq, mmureq->id, std::nullopt);
        mmu_ack->put(mmuack);
        return false;
    }

    auto tr = context(mmureq);
    TranslationState state{driver_port->asid(), tr};

    /* Detect duplicates and merge them into one OTB entry, if enabled */
    auto [otb_id, duplicated] = otb.try_combine(mmureq, state);

    if (!otb_id) {
        // If no duplicate was found and the OTB is not full, allocate a
        // new entry
        auto &mru = select_mru(mmureq->is_instruction);
        otb_id = otb.try_allocate(mmureq, mru, state, duplicated);
        if (otb_id)
            ev_pmu(mmureq, ev_num(mmureq->is_instruction ? archx::PMU::L2I_TLB : archx::PMU::L2D_TLB));
    }

    // otb_id will still be std::nullopt if no duplicate was found and the
    // OTB is full
    auto mmuack = make_child<MMUAck>(mmureq, mmureq->id, otb_id);
    mmu_ack->put(mmuack);

    return otb_id.has_value();
}

void MMU::accept_invalidate_requests()
{
    while (tlbi_request_in->can_take()) {
        auto tlbireq = tlbi_request_in->take();
        tlb_invalidate_event(tlbireq);
        tlb_invalidations.push(tlbireq);
    }
}

bool MMU::schedule_tlb_lookup(OTBEntry &otbentry)
{
    if (!tlb_lookup.can_put())
        return false;

    otbentry.lookup_size = otbentry.mru.current_size();
    otbentry.execute();
    tlb_lookup.put(otbentry.otb_id);
    return true;
}

void MMU::schedule_tlb_lookups()
{
    if (tlb_lookup.can_put())
        otb.schedule_entries([this](OTBEntry &e) { return this->schedule_tlb_lookup(e); },
                             [](const OTBEntry &e) { return e.can_schedule_lookup(); },
                             lookup_candidate);
}

void MMU::schedule_tlb_invalidations()
{
    // We cannot schedule an invalidation if anything is executing in the
    // pipelines, including another invalidation
    if (invalidation_executing)
        return;

    for (auto &[_, otb_entry] : otb)
        if (otb_entry.executing)
            return;

    invalidation_executing = true;
    tlb_lookup.put(0);
}

bool MMU::schedule_walk(OTBEntry &otbentry)
{
    if (!walk_fsms.can_acquire() || !table_walk.can_put())
        return false;

    otbentry.execute();
    otbentry.fsm_credit = walk_fsms.acquire();
    table_walk.put(otbentry.otb_id);
    return true;
}

void MMU::schedule_walks()
{
    if (walk_fsms.can_acquire() && table_walk.can_put())
        otb.schedule_entries([this](OTBEntry &e) { return this->schedule_walk(e); },
                             [](const OTBEntry &e) { return e.can_schedule_walk(); },
                             walk_candidate);
}

void MMU::send_responses(std::deque<uint64_t> &response_queue, OutOpt<MMUResp> &mmu_response)
{
    while (mmu_response.bind_count() && !response_queue.empty() && mmu_response->can_put()) {
        auto otb_id = response_queue.front();
        auto &otbentry = otb.at(otb_id);
        auto mmu_req = otbentry.request;
        auto mmu_resp = make_child<MMUResp>(mmu_req,
                                            otbentry.otb_id,
                                            mmu_req->virt_addr,
                                            mmu_req->is_instruction,
                                            mmu_req->is_store,
                                            otbentry.phys_addr,
                                            otbentry.page_size,
                                            otbentry.asid,
                                            otbentry.faulted);
        mmu_response->put(mmu_resp);
        response_queue.pop_front();
        otb.deallocate(otbentry);
    }
}

void MMU::collect_pmu_data()
{
    if (enable_per_cycle_pmu) {
        for (auto &[_, otb_entry] : otb) {
            if (otb_entry.op == MMUOp::PageWalk) {
                ev_pmu(otb_entry.request,
                       ev_num(otb_entry.request->is_instruction ? archx::PMU::ITLB_WALK_PERCYC
                                                                : archx::PMU::DTLB_WALK_PERCYC));
            }
        }
    }
}

void MMU::dump(std::ostream &os) const
{
    fmt::print(os, "= MMU ======================================================\n");
    otb.dump(os);
    imru.dump(os);
    dmru.dump(os);
    fmt::print(os, "============================================================\n");
}

// called after all objects exist and before simulation starts
void MMU::end_of_elaboration()
{
    // If both request ports are connected, then we are a unified MMU
    unified_mmu = inst_mmu_request.bind_count() && data_mmu_request.bind_count();
}

TranslationContext MMU::context(const MMUReq &req) const
{
    return {req->virt_addr, Bitfield{driver_port->read_sysreg(TCR_EL1)}, req->is_instruction};
}

Translation MMU::translation(const OTBEntry &otb_entry) const
{
    return {otb_entry.request->virt_addr,
            Bitfield{driver_port->read_sysreg(TCR_EL1)},
            Bitfield{driver_port->read_sysreg(TTBR0_EL1)},
            Bitfield{driver_port->read_sysreg(TTBR1_EL1)},
            otb_entry.request->is_instruction};
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
