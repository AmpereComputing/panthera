//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <fmt/format.h>
#include <memory>
#include "ampere/core/core.h"
#include "ampere/core/idr/throttling.h"
#include "ampere/core/ixu.h"
#include "ampere/core/ixu_defs.h"
#include "ampere/core/prefetchers/l1p_l2p_translator.h"
#include "ampere/core/xu.h"
#include "ampere/sc/ancestor.h"

namespace ampere::core {

Core::Core(const sc_core::sc_module_name& name) : Module(name)
{
    construct();
}

Core::Core(const sc_core::sc_module_name& name, Clock* clock, Environment* env) : Module(name, clock, env)
{
    construct();
}

Core::Core(const sc_core::sc_module_name& name, Clock* clock) : Module(name, clock)
{
    construct();
}

void Core::construct()
{
    if (trap_cycles) {
#if defined(AMPERE_TRAP_ON_CYCLE)
        get_clock()->trap_cycle(*trap_cycles);
#else
        std::cerr << "trap_cycles only supported in debug builds\n";
        exit(1);
#endif
    }

    ixu = std::make_unique<IXU>("ixu");

    if (perfect_frontend) {
        pfe = std::make_unique<PerfectFE>("pfe");
    } else {
        bpu = std::make_unique<BPU>("bpu");
        icf = std::make_unique<ICF>("icf");
        immu = std::make_unique<MMU>("immu");
    }

    // Connections for shared or separate IMMU
    if (perfect_frontend) {
        // "IMMU" connections
        connect(pfe->pte_fetch_out, l2c.immu_request).name("pfe_immu_to_l2_request");
        connect(l2c.immu_response, pfe->pte_fetch_in).name("l2_to_pfe_immu_response");
        connect(lsu.tlbi_immu_request_out, pfe->tlbi_request).name("lsu_to_pfe_tlbi_request");
    } else {
        // For unified_mmu, the IMMU will never get any requests and so these interfaces will not
        // be used, except it will harmlessly continue to process invalidations from LSU.
        connect(immu->pte_fetch_out, l2c.immu_request).name("immu_to_l2_request");
        connect(l2c.immu_response, immu->pte_fetch_in).name("l2_to_immu_response");
        connect(lsu.tlbi_immu_request_out, immu->tlbi_request_in).name("lsu_to_immu_tlbi_request");

        if (unified_mmu) {
            // I-side connections for shared MMU -- the MMU's first interface goes to ICF
            connect(dmmu.inst_mmu_ack, icf->mmu_ack).name("icf_immu_ack");
            connect(dmmu.inst_mmu_response, icf->mmu_response).name("icf_immu_response");
            connect(dmmu.inst_tlbi_request_out, icf->tlbi_request).name("immu_to_icf_tlb_invalidation");
            connect(icf->mmu_request, dmmu.inst_mmu_request).name("icf_immu_request");
        } else {
            // ICF connections for separate IMMU
            connect(immu->inst_mmu_ack, icf->mmu_ack).name("icf_immu_ack");
            connect(immu->inst_mmu_response, icf->mmu_response).name("icf_immu_response");
            connect(immu->inst_tlbi_request_out, icf->tlbi_request).name("immu_to_icf_tlb_invalidation");
            connect(icf->mmu_request, immu->inst_mmu_request).name("icf_immu_request");
        }
    }

    // Connections for shared or separate DMMU -- the data-side connections are the same either way
    connect(dmmu.data_mmu_ack, lsu.mmu_miss_ack).name("lsu_to_dmmu_ack");
    connect(dmmu.data_mmu_response, lsu.mmu_miss_response).name("lsu_to_dmmu_response");
    connect(dmmu.data_tlbi_request_out, lsu.tlbi_request_in).name("dmmu_to_lsu_tlb_invalidation");
    connect(dmmu.pte_fetch_out, l2c.dmmu_request).name("dmmu_to_l2_request");
    connect(l2c.dmmu_response, dmmu.pte_fetch_in).name("l2_to_dmmu_response");
    connect(lsu.mmu_miss_request, dmmu.data_mmu_request).name("lsu_to_dmmu_request");
    connect(lsu.tlbi_request_out, dmmu.tlbi_request_in).name("lsu_to_dmmu_tlbi_request");
    if (lsu.otb_full_sleep)
        connect(dmmu.mmu_can_accept, lsu.mmu_can_accept).name("lsu_to_dmmu_can_accept");

    if (perfect_frontend) {
        // "BPU" connections
        connect(idr.feq_credit_return, pfe->feq_credit_return).name("feq_credit_return_fifo");
        connect(idr.bob_alloc, pfe->bob_alloc).name("bob_alloc");
        connect(pfe->bob_credit_return, idr.bob_credit_return)
            .name("bob_credit_return_fifo")
            .bandwidth(gpc.uop_retire_width * 2);
        connect(idr.frontend_stall, pfe->frontend_stall).name("frontend_stall");
        connect(gpc.instructions_committed_port, pfe->instructions_committed).name("instructions_committed_port");

        // "ICF" connections
        connect(pfe->iq_write_port, idr.in).name("pfe_out_to_idr_in_fifo");
        connect(idr.instruction_credit_return, pfe->instruction_credit_return).name("instruction_fifo_credit_return");
        connect(pfe->miss_request, l2c.icf_request).name("pfe_to_l2_request");
        connect(pfe->pmu_idr_icache_miss, {&idr.pmu_frontend_cache_miss}).name("pfe_to_idr_pmu_cache_miss");
        connect(pfe->pmu_idr_l2_miss, {&idr.pmu_frontend_l2_miss}).name("pfe_to_idr_pmu_l2_miss");
        connect(pfe->pmu_idr_tlb_miss, {&idr.pmu_frontend_tlb_miss}).name("pfe_to_idr_pmu_tlb_miss");
        connect(pfe->pmu_idr_feq_empty, {&idr.pmu_frontend_feq_empty}).name("pfe_to_idr_feq_empty");
        connect(pfe->pmu_idr_mem_stall, {&idr.pmu_frontend_mem_stall}).name("pfe_to_idr_mem_stall");
        connect(pfe->pmu_idr_btc_clear, {&idr.pmu_btc_clear}).name("pfe_to_idr_btc_clear");
        connect(pfe->pmu_idr_btb_clear, {&idr.pmu_btb_clear}).name("pfe_to_idr_btb_clear");
    } else {
        // BPU connections
        connect(bpu->prediction, icf->fetch_port).name("bpu_to_icf_fetch_fifo");
        connect(bpu->clear, icf->bp_clear_port).name("bp_clear");
        connect(bpu->ittage_stall, icf->ittage_stall_port).name("ittage_stall");
        connect(idr.feq_credit_return, bpu->feq_credit_return).name("feq_credit_return_fifo");
        connect(idr.bob_alloc, bpu->bob_alloc).name("bob_alloc");
        connect(bpu->bob_credit_return, idr.bob_credit_return).name("bob_credit_return_fifo");
        connect(idr.frontend_stall, bpu->frontend_stall).name("frontend_stall");
        connect(icf->icache_misses, bpu->icache_miss_port).name("icache_misses");
        connect(gpc.instructions_committed_port, bpu->instructions_committed_port).name("instructions_committed_port");
        connect(icf->br_check_flush_port, bpu->br_check_flush).name("icf_br_check_flush");
        connect(icf->loop_fetch_port, bpu->loop_fetch).name("loop_fetch").bandwidth(256);
        connect(icf->loop_restart, bpu->loop_restart).name("icf_loop_restart");

        // ICF connections
        connect(icf->miss_request, l2c.icf_request).name("icf_to_l2_request");
        connect(l2c.icf_request_schedule, icf->miss_request_scheduled).name("icf_to_l2_request_scheduled");
        connect(icf->iq_write_port, idr.in).name("icf_out_to_idr_in_fifo");
        connect(idr.instruction_credit_return, icf->instruction_credit_return).name("instruction_fifo_credit_return");
        connect(icf->pmu_idr_icache_miss, {&idr.pmu_frontend_cache_miss}).name("icf_to_idr_pmu_cache_miss");
        connect(icf->pmu_idr_l2_miss, {&idr.pmu_frontend_l2_miss}).name("icf_to_idr_pmu_l2_miss");
        connect(icf->pmu_idr_tlb_miss, {&idr.pmu_frontend_tlb_miss}).name("icf_to_idr_pmu_tlb_miss");
        connect(icf->pmu_idr_feq_empty, {&idr.pmu_frontend_feq_empty}).name("icf_to_idr_feq_empty");
        connect(icf->pmu_idr_mem_stall, {&idr.pmu_frontend_mem_stall}).name("icf_to_idr_mem_stall");
        connect(icf->pmu_idr_btc_clear, {&idr.pmu_btc_clear}).name("icf_to_idr_btc_clear");
        connect(icf->pmu_idr_btb_clear, {&idr.pmu_btb_clear}).name("icf_to_idr_btb_clear");

        icf->l2_indexer.bind(l2c);
        icf->coherency_validator.bind(l2c);
    }

    // IDR connections
    connect(idr.out, ixu->dispatch_port).name("dispatch");
    connect(idr.gpc_dispatch_port, gpc.dispatch_port).name("gpc_dispatch");
    connect(idr.flush_req_to_gpc, gpc.flush_req_from_idr).name("idr_to_gpc_flush_req");

    // initialize IXU and pass over the number of scheduler entries from the credit pool
    Scheduler::set_active_partition_counts(IXU_PARTITIONS, idr.fsu_partitions);
    ixu->init(idr.sch_size());
    connect(ixu->credit_return, idr.ixu_credit_return)
        .name("ixu_credit_return")
        // bandwidth is: (group width) * (groups which can be deallocated per cycle)
        // The number of deallocatable groups is the number of scheduler tables
        // plus 2 because each of the two memory tables can deallocate up to two groups.
        // I added one more for slack.
        .bandwidth(MAX_GROUP_SIZE * (ixu->num_execution_units + 3));
    connect(gpc.idr_uop_return, idr.rob_uop_return)
        .name("rob_uop_return")
        .bandwidth(gpc.uop_retire_width * 2); // Cleaving may return 2 uops' worth of resources for each one committed

    Scheduler::set_fsu_by_enables(idr.u_pipe_enable,
                                  idr.v_pipe_enable,
                                  idr.w_pipe_enable,
                                  idr.x_pipe_by_enable,
                                  idr.y_pipe_by_enable,
                                  idr.loads_on_e_pipe,
                                  ixu->uvw_load_port_share);

    // throttle signals from IXU and FSU back to IDR, for power reduction
    if (auto* throttler = dynamic_cast<IDRWidthThrottleIXUFSUThroughput*>(idr.width_throttle.get()); throttler) {
        connect(ixu->idr_throttle_ixu, throttler->ixu_throttle).name("ixu_to_idr_throttle_request");
        connect(ixu->idr_throttle_fsu, throttler->fsu_throttle).name("fsu_to_idr_throttle_request");
    }

    // signals for STALL_BACKEND_ILOCK and STALL_BACKEND_BUSY pmus
    connect(ixu->pmu_stall_xu_busy, idr.pmu_sched_stall_xu_busy).name("pmu_sched_to_idr_xu_busy");
    connect(ixu->pmu_stall_ilock, idr.pmu_sched_stall_ilock).name("pmu_sched_to_idr_ilock");

    // IXU to XU and LSU connections and writebacks

    // issue from AGUs to LSU
    for (auto i = 0u; i < ixu->num_execution_units; i++) {
        auto port_name = fmt::format("agu{}", i);
        connect(ixu->mem_issue_port[i], lsu.issue_mem[i]).name(port_name);
    }
    // writeback from LSU to scheduler
    for (auto i = 0u; i < ixu->num_execution_units; i++) {
        auto port_name = fmt::format("lsu_to_ixu_mem{}_writeback", i);
        connect(lsu.writeback_mem[i], ixu->writeback_xu[i]).name(port_name);

        port_name = fmt::format("lsu_to_ixu_mem{}_complete", i);
        connect(lsu.complete_mem[i], ixu->complete_xu[i]).name(port_name);
    }
    connect(lsu.load_cancel_out, ixu->load_cancel_in).name("load_cancel").bandwidth(lsu.load_schedule_bandwidth());

    // XU mispredict ports to the GPC
    for (auto i = 0u; i < ixu->num_execution_units; i++) {
        auto port_name = fmt::format("branch_mispredict_{}", i);
        connect(ixu->xu[i].mispredict_port, gpc.mispredict_port[i]).name(port_name);
    }
    for (auto i = 0u; i < ixu->num_execution_units; i++) {
        auto port_name = fmt::format("indirect_branch_mispredict_{}", i);
        connect(ixu->xu[i].mispredict_indirect_port, gpc.mispredict_indirect_port[i]).name(port_name);
    }
    // If the latency of the mispredict port is 0, we need to manually notify
    // the clock of the ordering. This is because of how the XU is nested
    // inside the IXU---the clock won't otherwise know to order IXU vs GPC. If
    // the latency is not 0, this shouldn't really hurt.
    get_clock()->add_zdelay_connection(ixu.get(), &gpc);

    // IXU commit port to the GPC
    connect(ixu->commit_port, gpc.commit_port).name("ixu_to_gpc_commit");

    // GPC retire to LSU
    connect(gpc.loads_committed_port, lsu.loads_committed_port).name("loads_committed");
    connect(gpc.stores_committed_port, lsu.stores_committed_port).name("stores_committed");
    if (perfect_frontend)
        connect(gpc.branches_committed_port, pfe->branches_committed).name("branches_committed");
    else
        connect(gpc.branches_committed_port, bpu->branches_committed).name("branches_committed");

    // Latency is to cover the time from commit of a store until it shows up in the PWC,
    // and also a couple cycles because the LSU does not model non-spec sleep correctly,
    // so uops waiting for non-spec indication from GPC wake up 2 cycles too fast.
    connect(gpc.lsu_next_to_commit_port, lsu.next_to_commit_in).name("next_to_commit").latency(4);
    connect(gpc.rob_empty_out, idr.rob_empty_in).name("rob_empty");

    // GPC flush connections
    if (perfect_frontend) {
        connect(gpc.flush_port,
                {&pfe->flush_port, &idr.flush, &ixu->flush_port, &lsu.flush, &dmmu.flush_port, &gpc.flush_read_port})
            .name("flush_port");
    } else {
        connect(gpc.flush_port,
                {&bpu->flush,
                 &icf->flush_port,
                 &immu->flush_port,
                 &idr.flush,
                 &ixu->flush_port,
                 &lsu.flush,
                 &dmmu.flush_port,
                 &gpc.flush_read_port})
            .name("flush_port");
    }

    // LSU connections
    connect(lsu.credit_return, idr.lsu_credit_return)
        .name("lsu_credit_return")
        .bandwidth(idr.lob_size() + idr.sob_size());
    connect(lsu.l2_load_miss_request, l2c.lsu_read_request).name("lsu_to_l2_load_request");
    connect(lsu.l2_store_miss_request, l2c.lsu_write_request).name("lsu_to_l2_store_request");
    connect(lsu.l2_prefetch_request, l2c.lsu_prefetch_request)
        .name("lsu_to_l2_prefetch_request")
        .bandwidth(lsu.l1d_load_miss_bandwidth());
    connect(lsu.l2_writeback_request, l2c.lsu_evict_request).name("lsu_to_l2_writeback_request");
    connect(lsu.pmu_oldest_load_tlb_out, {&idr.pmu_oldest_load_tlb_miss}).name("lsu_to_idr_pmu_tlb_miss");
    connect(lsu.pmu_oldest_load_cache_out, {&idr.pmu_oldest_load_cache_miss}).name("lsu_to_idr_pmu_cache_miss");
    connect(lsu.pmu_oldest_load_ofb_full_out, {&idr.pmu_oldest_load_ofb_full}).name("lsu_to_idr_pmu_ofb_full");
    connect(lsu.pmu_oldest_load_mem_out, {&idr.pmu_oldest_load_mem_miss}).name("lsu_to_idr_pmu_mem_miss");
    connect(lsu.pmu_oldest_ldst_valid_out, {&idr.pmu_oldest_ldst_valid}).name("lsu_to_idr_pmu_oldest_ldst_valid");
    connect(lsu.pmu_oldest_ldst_atomic_out, {&idr.pmu_oldest_ldst_atomic}).name("lsu_to_idr_pmu_oldest_ldst_atomic");
    connect(lsu.pmu_oldest_load_st_wait, {&idr.pmu_oldest_load_st_wait}).name("lsu_to_idr_pmu_oldest_load_st_wait");
    connect(l2c.l2_no_pending_stores_out, lsu.l2_no_pending_stores_in).name("l2_to_lsu_no_pending_stores");
    // Bandwidth 2 to cover rare cases where we deallocate and receive a barrier in the same cycle
    connect(lsu.lsu_has_barrier_out, l2c.lsu_has_barrier_in).name("lsu_to_l2_barrier").bandwidth(2);
    connect(l2c.l2_barrier_out, lsu.l2_barrier_in).name("l2_to_lsu_barrier");
    connect(l2c.l2_store_release_out, lsu.l2_store_release_in).name("l2_to_lsu_store_release");
    connect(l2c.l2_store_exclusive_out, lsu.l2_store_exclusive_in).name("l2_to_lsu_store_exclusive").latency(0);
    connect(l2c.l2_miss_out, {&lsu.pmu_l2_miss_in}).name("l2_to_lsu_miss");

    if (auto* va_prefetch = get_nearest_descendant<L1P_L2P_Translator>(lsu.prefetcher.get()); va_prefetch) {
        connect(l2c.prefetcher->translate_prefetch_request, va_prefetch->requests).name("l2_to_lsu_va_prefetch");
    }

    // Raw scrub bandwidth is one for every load pipe plus one for snoops
    connect(lsu.raw_scrub_port, gpc.raw_scrub_port).name("lsu_to_gpc_raw_scrub").bandwidth(lsu.load_pipes + 1);
    connect(idr.lsu_mem_rob_preview_port, lsu.mem_rob_preview_port).name("idr_to_lsu_mem_rob_preview");
    connect(lsu.lob_balance_out, idr.lob_balance_in).name("lsu_to_idr_lob_balance");

    lsu.l2_indexer.bind(l2c);
    lsu.coherency_validator.bind(l2c);

    // L2 cache connections
    connect(l2c.lsu_read_response, lsu.l2_load_miss_response).name("lsu_to_l2_load_response");
    connect(l2c.lsu_snoop_request, lsu.l2_snoop_request).name("l2_to_lsu_snoop_request").latency(0);
    if (perfect_frontend) {
        connect(l2c.icf_response, pfe->miss_response).name("pfe_to_l2_response");
        connect(l2c.icf_snoop_request, pfe->snoop_port).name("l2_to_pfe_snoop_request").latency(0);
        connect(l2c.l2_miss_out_to_icf, {&pfe->pmu_l2_miss_in_icf}).name("l2_to_pfe_miss");
        l2c.l1i_lookup.bind(*pfe);
    } else {
        connect(l2c.icf_response, icf->miss_response).name("icf_to_l2_response");
        connect(l2c.icf_snoop_request, icf->snoop_port).name("l2_to_icf_snoop_request").latency(0);
        connect(l2c.l2_miss_out_to_icf, {&icf->pmu_l2_miss_in_icf}).name("l2_to_icf_miss");
        l2c.l1i_lookup.bind(*icf);
    }
    l2c.l1d_lookup.bind(lsu);

    l2c.set_node_id(node_id);

    bool help = get_environment()->parameters.help_requested();
    if (!help && instruction_stream_name.val().empty()) {
        std::cerr << "Error: -instruction_stream_name not specified.\n";
        exit(1);
    }

    if (lsu.enable_mte && !l2c.disable_make_unique)
        AMPERE_ERROR("MakeUnique must be disabled when MTE is on. Set l2c.disable_make_unique = true.");

    if (!instruction_stream_name.val().empty()) {
        driver = archx::makeDriver(instruction_stream_name,
                                   skip_instructions,
                                   speculative_memory_image,
                                   use_wrong_path_emulation,
                                   validate_emulation,
                                   mem_image_path);
        // The LSU oracle driver does not speculate and thus does not care about wrong path.
        lsu_oracle_driver = archx::makeDriver(instruction_stream_name,
                                              skip_instructions,
                                              false /*speculative_memory_image*/,
                                              false /*use_wrong_path_emulation*/,
                                              false /*validate_emulation*/,
                                              mem_image_path);

        // The L2 oracle driver does not speculate and thus does not care about wrong path.
        l2_oracle_driver = archx::makeDriver(instruction_stream_name,
                                             skip_instructions,
                                             false /*speculative_memory_image*/,
                                             false /*use_wrong_path_emulation*/,
                                             false /*validate_emulation*/,
                                             mem_image_path);
    } else {
        driver = std::make_unique<archx::DummyDriver>();
        lsu_oracle_driver = std::make_unique<archx::DummyDriver>();
        l2_oracle_driver = std::make_unique<archx::DummyDriver>();
    }

    // Driver connections
    if (perfect_frontend) {
        pfe->driver.bind(*driver);
    } else {
        bpu->driver.bind(*driver);
        icf->driver_port.bind(*driver);
        immu->driver_port.bind(*driver);
    }

    dmmu.driver_port.bind(*driver);
    idr.driver_port.bind(*driver);
    idr.pac_mode.driver_port.bind(*driver);
    gpc.driver_port.bind(*driver);
    lsu.driver_port.bind(*driver);
    lsu.oracle_driver_port.bind(*lsu_oracle_driver);
    l2c.oracle_driver.bind(*l2_oracle_driver);
    l2c.real_driver.bind(*driver);
}

void Core::end_of_elaboration()
{
    if (decoders.val().empty())
        AMPERE_ERROR("no decoders were specified");

    AMPERE_ASSERT(ixu->num_execution_units == gpc.num_execution_units,
                  "num_execution_units must be the same in all modules");
    AMPERE_ASSERT(ixu->num_execution_units == lsu.num_execution_units,
                  "num_execution_units must be the same in all modules");
    AMPERE_ASSERT(
        ixu->num_execution_units > TOTAL_IXU_PIPES,
        "num_execution_units param must be at least TOTAL_IXU_PIPES so that loads and stores can write back ({} vs {})",
        ixu->num_execution_units,
        TOTAL_IXU_PIPES);

    archx::Instruction::set_decoders(decoders);
}

void Core::end_of_simulation()
{
    Cycle end = get_cycle();
    sim_end_event(total_cycles(end));
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
