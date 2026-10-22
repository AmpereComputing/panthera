//
// Copyright (c) 2018-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/core/idr.h"

#include <ranges>
#include <sstream>
#include <utility>
#include "ampere/archx/pmu.h"
#include "ampere/archx/instruction_is.h"
#include "ampere/archx/register.h"
#include "ampere/archx/uop_info.h"
#include "ampere/core/ixu_defs.h"
#include "ampere/adis/adis-encoding-fields.h"
#include "ampere/sc/report.h"
#include "ampere/util/enum.h"

// #define DEBUG_IDR_ALIGN
#if defined(DEBUG_IDR_ALIGN)
#define DBG_ALGN(format_str, ...) fmt::print(stderr, "{}: " format_str "\n", get_cycle(), ##__VA_ARGS__)
#else
#define DBG_ALGN(format_str, ...)
#endif

// #define DEBUG_IDR_PMU
#if defined(DEBUG_IDR_PMU)
#define DBG_IPMU(format_str, ...) fmt::print(stderr, "{}: " format_str "\n", get_cycle(), ##__VA_ARGS__)
#else
#define DBG_IPMU(format_str, ...)
#endif

// #define DEBUG_IDR_FUSION // or cleaving
#if defined(DEBUG_IDR_FUSION)
#define DBGFUS(format_str, ...) fmt::print(stderr, "{}: FUSION " format_str "\n", get_cycle(), ##__VA_ARGS__)
#else
#define DBGFUS(format_str, ...)
#endif

// #define DEBUG_CREDIT
#if defined(DEBUG_CREDIT)
#define DBGCRT(format_str, ...) fmt::print(stderr, "{}: " format_str "\n", fmt_cycle(), ##__VA_ARGS__)
#else
#define DBGCRT(format_str, ...) \
    do {                        \
    } while (0)
#endif

// #define DEBUG_IDR
// #define DEBUG_DECODE
namespace ampere::core {

static const size_t DECODE_THROTTLE_WIDTH = 2;

std::pair<int, int> IDR::UopGroup::lane_to_bank(unsigned lane) const
{
    if (lanes.size() == 4)
        return {(lanes[lane] & 0x2) >> 1, lanes[lane] & 0x1};

    const unsigned lanes_per_partition = lanes.size() / MAX_PARTITIONS;
    return {(int)(lanes[lane] / lanes_per_partition), (int)(lanes[lane] % lanes_per_partition)};
}

void IDR::UopGroup::assign_banks()
{
    for (unsigned i = 0, l = 0; i < uops.size(); i++) {
        auto [partition, lane] = lane_to_bank(l);
        uops[i]->decode.partition = partition;
        uops[i]->decode.lane = lane;

        // If the next uop is cleaved, it is in the same lane as the previous one
        if (!uops[i]->cleaved_first_half)
            ++l;
    }
}

void IDR::UopGroup::assign_pre_swizzle_banks()
{
    for (unsigned i = 0, l = 0; i < uops.size(); i++) {
        auto [partition, lane] = lane_to_bank(l);
        uops[i]->decode.pre_swizzle_partition = partition;
        uops[i]->decode.pre_swizzle_lane = lane;

        // If the next uop is cleaved, it is in the same lane as the previous one
        if (!uops[i]->cleaved_first_half)
            ++l;
    }
}

unsigned IDR::UopGroup::count_instructions() const
{
    unsigned count = 0;
    for (const auto& u : uops)
        count += u->archx_uop->end_inst;
    return count;
}

const decltype(scheduler_enum()) IDR::scheduler_enumeration = scheduler_enum();

const util::FrozenEnumeration<unsigned> IDR::resource_enumeration = []() {
    auto builder = util::FrozenEnumeration<unsigned>::create();
    builder.extend_all_names(scheduler_enumeration, "Sch");
    builder.extend_names<BufferResource>();
    builder.extend_names<SchedulerType>({}, {}, "Sch_BW_");
    return builder.freeze();
}();

IDR::IDR(const sc_core::sc_module_name& name)
    : Module(name),
      random(random_jitter),
      sch_assigned_this_cycle(sch.size()),
      load_balancing(uop_decode_width),
      counter_balancing(uop_decode_width)
{
    if (get_environment()->parameters.help_requested())
        return;

    AMPERE_ASSERT(rf_int.get_size() + rf_fp.get_size() + rf_flag.get_size() + rf_pred.get_size() < MAX_PRF_SIZE,
                  "Too many PRF entries for IXU dependency tracker, increase MAX_PRF_SIZE");

    std::fill(next_div_scheduler.begin(), next_div_scheduler.end(), Sch::X);

    // For dependency tracking in IXU, all the PRF entries need to have unique IDs, and ID 0
    // is reserved for invalid sources. Change the IDs of the vector PRF to come after integer,
    // and flags after that.
    rf_int.set_base_id(1);
    rf_fp.set_base_id(rf_int.get_size() + 1);
    rf_flag.set_base_id(rf_int.get_size() + rf_fp.get_size() + 1);
    rf_pred.set_base_id(rf_int.get_size() + rf_fp.get_size() + rf_flag.get_size() + 1);

    AMPERE_ASSERT(uop_decode_width % 2 == 0, "Decoder count must be even");

    gather_tbl.set_base_id(0);

    // IDR-executed uops bypass the IXU, which normally covers the tail end of IDR and beginning of GPC
    // pipelines. Add 3 cycles to cover DR05, GF01, and GF02 before commit in GF03.
    connect(idr_uop_delay_out, idr_uop_delay_in).name("idr_uop_delay").bandwidth(64).latency(3);

    resource_total.resize(RESOURCE_COUNT);
    resource_maxed.resize(RESOURCE_COUNT);

    // default width for autonomous decode/issue slowdown for power reduction
    current_decode_width = uop_decode_width;

    AMPERE_ASSERT(static_cast<bool>(counter_balancing) != (load_balancing || swizzle) || cross_pipe,
                  "Should only enable one of counter balancing and (load balance, swizzle) {} {} {}",
                  static_cast<bool>(counter_balancing),
                  static_cast<bool>(load_balancing),
                  static_cast<bool>(swizzle));
    AMPERE_ASSERT(!(counter_balancing && uop_decode_width != 4), "Counter balancing only works for 4-wide decode");
}

IDR::~IDR()
{
    for (auto& i : committed_rf_table)
        if (i)
            release_credit(i);
}

std::vector<size_t> IDR::sch_size() const
{
    std::vector<size_t> vec(sch.size());
    for (auto [i, v] : std::views::enumerate(vec))
        v = sch[i].size;

    return vec;
}

void IDR::start_of_simulation()
{
    // Reserve 38 of the integer register file entries for the logical registers:
    // X0 - X30, four copies of SP (one per EL), RGSR (memory tagging), and two temps
    for (int r = archx::LReg::L0; r <= archx::LReg::L30; r++)
        committed_rf_table[r] = rf_int.acquire();
    committed_rf_table[archx::LReg::s0] = rf_int.acquire();
    committed_rf_table[archx::LReg::s1] = rf_int.acquire();
    committed_rf_table[archx::LReg::SP] = rf_int.acquire();
    committed_rf_table[archx::LReg::SP_EL1_REG] = rf_int.acquire();
    committed_rf_table[archx::LReg::SP_EL2_REG] = rf_int.acquire();
    committed_rf_table[archx::LReg::SP_EL3_REG] = rf_int.acquire();
    committed_rf_table[archx::LReg::RGSR_REG] = rf_int.acquire();

    // The zero register and invalid register need a dummy PRF entry for move elimination, but nothing
    // will actually ever write to them. Just duplicate an existing unused entry.
    committed_rf_table[archx::LReg::ZR] = rf_int.duplicate(committed_rf_table[archx::LReg::SP_EL3_REG]->get_id());
    committed_rf_table[archx::LReg::INVALID] = rf_int.duplicate(committed_rf_table[archx::LReg::SP_EL3_REG]->get_id());

    // Reserve 1 of the flag register file entries for the architectural flags
    committed_rf_table[archx::LReg::FLAGS] = rf_flag.acquire();
    // ... and one for the flags temp (trinity sve, onwards)
    if (tmp_flag_enable) {
        committed_rf_table[archx::LReg::FLGT0] = rf_flag.acquire();
    }

    // Reserve some phys regs for predicate registers in models that have predicate registers
    if (rf_pred.get_size()) {
        for (int r = archx::LReg::P0; r <= archx::LReg::P15; r++) {
            committed_rf_table[r] = rf_pred.acquire();
        }
        committed_rf_table[archx::LReg::FFR] = rf_pred.acquire();
    }

    // Reserve 37 of the floating point register file entries for the logical registers:
    // V0 - V31, and 5 temps
    for (int r = archx::LReg::V0; r <= archx::LReg::V31; r++)
        committed_rf_table[r] = rf_fp.acquire();
    for (int r = archx::LReg::t0; r <= archx::LReg::t4; r++)
        committed_rf_table[r] = rf_fp.acquire();

    // Speculative PRF table starts out the same as the committed table
    for (size_t i = 0; i < util::enum_max_numeric_size<archx::LReg>(); i++)
        spec_rf_table[i] = committed_rf_table[i] ? committed_rf_table[i]->get_id() : 0;
}

bool IDR::run()
{
    reclaim_credits();

    // Read instructions queue.
    // The port size is set to the maximum number of instructions we can handle
    // in a cycle.  However, it is possible we can't handle all instructions in
    // a cycle so we need to queue them up.
    while (in->can_take()) {
        instruction_fifo.push_back(in->take());
    }

    // Get any changes in the rob_empty signal from GPC
    while (rob_empty_in->can_take())
        rob_empty = rob_empty_in->take();

    // Complete any uops which are executed in IDR and so not sent to IXU
    while (idr_uop_delay_in->can_take()) {
        auto uop = idr_uop_delay_in->take();
        uop->complete = true;
        ev_idr_uop(uop);
    }

    throttle_check();
    handle_flush_requests();
    pmu_events();

    idr_pipe.run(); // align() -> decode() -> rename()

    // LSU says that pipe1 was blocked, so we should bias the load balancing toward partition 0
    if (lob_balance_in->can_take()) {
        (void)lob_balance_in->take();
        counter_balancing.bias_counter(Uoppipe::C, -1);
    }

    // count the number of cycles we are in throttle mode
    if (DECODE_THROTTLE_WIDTH == current_decode_width) {
        ev_pmu(ev_num(archx::PMU::IDR_THROTTLE_CYCLES));
    }

    if (detailed_stats_collection) {
        stats_collection();
        return true; // always run
    }

    return true;
}

void IDR::handle_flush_requests()
{
    if (flush->can_read()) {
        flush_data = flush->read();
        DBG_GATHERC("idr flush. type {}", flush_data->type);
        do_flush = true;

        flush_cycle = get_cycle();
        check_for_resources = true;
        in->clear();
        waiting_for_flush = false;
        previous_pc = 0x0_pc;

        // Free any ICF credits belonging to uops in the pipeline
        for (auto& pipe_group : idr_pipe) {
            for (auto& uop : pipe_group.uops) {
                if (uop->archx_uop->end_inst) {
                    for (const auto& inst : uop->instructions) {
                        if (inst->fifoid)
                            release_credit(inst->fifoid);
                        if (inst->is_fetch_exit() && inst->fetch->feq_credit)
                            release_credit(inst->fetch->feq_credit);
                    }
                }
            }
        }
        idr_pipe.clear();

        stall_idr_fsu = false;
        fsu_schedulers_free = true;

        for (auto& instruction : instruction_fifo) {
            release_credit(instruction->fifoid);

            if (instruction->is_fetch_exit()) {
                if (instruction->fetch->feq_credit)
                    release_credit(instruction->fetch->feq_credit);
            }
        }
        instruction_fifo.clear();
        pending_uops.clear();

        if (!swob.empty()) {
            // Non-speculative flushes will clear the SWOB. Speculative flushes will not clear the SWOB,
            // but all entries younger than the flushing uop will be invalidated
            if (is_nonspeculative_flush(flush_data->type)) {
                ev_swob_flush();
                swob.clear();
            } else {
                Uop uop;
                for (auto swob_iter = swob.begin(); swob_iter != swob.end();) {
                    uop = *swob_iter;
                    if (uop->get_alloc_id() > flush_data->alloc_id) {
                        swob_iter = swob.erase(swob_iter);
                    } else {
                        ++swob_iter;
                    }
                }
            }
        }

        // The alloc_id that comes with the flush is the youngest uop
        // which survives the flush.  Find the first uop younger than
        // that, and delete it and everything before it (younger
        // still) from our list of dispatched-but-not-committed uops.
        uint64_t first_flushed_uop;
        for (first_flushed_uop = 0; first_flushed_uop < dispatched_uops.size(); first_flushed_uop++) {
            const auto& uop = dispatched_uops[first_flushed_uop];
            if (uop->get_alloc_id() > flush_data->alloc_id) {
                break;
            }
        }

        if (first_flushed_uop < dispatched_uops.size())
            next_rob_entry = dispatched_uops[first_flushed_uop]->rob_entry;

        for (uint64_t i = first_flushed_uop; i < dispatched_uops.size(); i++) {
            if (dispatched_uops[i]->is_load()) {
                lob_id_counter = dispatched_uops[i]->lob_id_val;
                lob_alloc_id_counter = dispatched_uops[i]->lob_alloc_id_val;
                break;
            }
        }

        dispatched_uops.erase(dispatched_uops.begin() + first_flushed_uop, dispatched_uops.end());

        // Reconstruct the speculative register table at the point of the flush. First reset it
        // to the committed table, then replay all the (non-flushed) uops which have been dispatched.
        for (size_t i = 0; i < util::enum_max_numeric_size<archx::LReg>(); i++)
            spec_rf_table[i] = committed_rf_table[i] ? committed_rf_table[i]->get_id() : 0;

        for (const auto& uop : dispatched_uops) {
            if (uop->is_producer()) {
                if (uop->is_gather()) {
                    DBG_GATHERC(
                        "idr: reestablish unflushed gather register {} as speculative, "
                        "parent txid: {}, txid: {}, alloc_id: {}",
                        uop->reg_dst,
                        uop->get_txid_parent(),
                        uop->get_txid(),
                        uop->get_alloc_id());
                    GatherProxyCreditPtr gprox = uop->get_gather_proxy();
                    if (gprox->prfid_still_valid()) {
                        spec_rf_table[uop->reg_dst] = uop->get_prfid_id();
                    }
                } else {
                    spec_rf_table[uop->reg_dst] = uop->get_prfid_id();
                }
            }
            if (uop->has_dest2())
                spec_rf_table[uop->reg_dst2] = uop->get_prfid2_id();
            if (uop->is_flag_producer())
                spec_rf_table[archx::LReg::FLAGS] = uop->get_flagsid_id();
            if (uop->is_temp_flags_producer())
                spec_rf_table[archx::LReg::FLGT0] = uop->get_flagsid_id();
        }

        idr_no_restart = (flush_data->type == archx::FlushType::NO_RESTART);

        // After a non-speculative flush the back end is empty, so any history about partition balancing is irrelevant.
        // But there's still a param to disable it because this makes it harder to compare the balancing quality of
        // different algorithms.
        counter_balancing.flush(is_nonspeculative_flush(flush_data->type));

        // Now figure out how many pdests we have to recover, and whether the recovery walk will take
        // long enough to delay dispatch of new uops.
        // The recovery walk starts from the last ROB ID which is a multiple of 32, or from the commit
        // point if that is closer.
        unsigned pdest_count{0};
        for (auto& uop : std::ranges::reverse_view(dispatched_uops)) {
            if (uop->is_producer())
                pdest_count++;
            if (uop->has_dest2())
                pdest_count++;
            if ((uop->rob_entry % 32) == 0)
                break;
        }

        // We can recovery 8 pdests per cycle
        auto stall_recovery_cnt = (pdest_count + 7) / 8;
        stall_recovery_end = get_cycle() + stall_recovery_cnt;
        stall_frontend_flush_end = std::max(stall_recovery_end, Cycle(get_cycle() + stall_frontend_flush_latency));

    } else {
        do_flush = false;
        flush_data = nullptr;
    }
}

void IDR::pmu_events()
{
    if (enable_per_cycle_pmu) {
        for (auto& instruction : instruction_fifo)
            ev_pmu(instruction, ev_num(archx::PMU::IQ_VALID_PERCYC));
    }

    // Collect STALL_BACKEND_ILOCK/BUSY PMU data in case we'll have a backend stall this cycle
    if (pmu_sched_stall_xu_busy->can_take())
        pmu_stall_busy = pmu_sched_stall_xu_busy->take();
    else
        pmu_stall_busy.clear();

    if (pmu_sched_stall_ilock->can_take())
        pmu_stall_ilock = pmu_sched_stall_ilock->take();
    else
        pmu_stall_ilock.clear();

    auto rename_group = idr_pipe[idr_pipe.size() - 1];
    bool rename_empty = !rename_group.initialized();

    if (pmu_btc_clear->can_read() && pmu_btc_clear->read())
        btc_clear_cycle = get_cycle();
    if (pmu_btb_clear->can_read() && pmu_btb_clear->read())
        btb_clear_cycle = get_cycle();

    bool in_btb_clear_window = false;
    bool in_btc_clear_window = false;
    if ((get_cycle() - btc_clear_cycle <= btc_clear_flush_width) && (btc_clear_cycle >= btb_clear_cycle))
        in_btc_clear_window = true;
    if ((get_cycle() - btb_clear_cycle <= btb_clear_flush_width) && (btb_clear_cycle > btc_clear_cycle))
        in_btb_clear_window = true;

    if (get_cycle() <= (improved_flow_tda_connor ? stall_frontend_flush_end : stall_recovery_end)) {
        ev_pmu(ev_num(archx::PMU::IDR_STALL_FLUSH));
        ev_pmu(ev_num(archx::PMU::STALL_FRONTEND_FLUSH));
        ev_pmu(ev_num(archx::PMU::STALL_FRONTEND_CPUBOUND));
    } else if (pending_uops.empty() && instruction_fifo.empty()) {
        // Count Level2 FE dispatch stall events (they will result in a frontend stall in 2 cycles).
        // STALL_FRONTEND_CACHE event (IMP def) removed from Trinity
        // STALL_FRONTEND_CACHE counts on IL1 miss even if L2 misses (ie counts
        //  as long as the IL1 miss entry is valid)
        if (pmu_frontend_l2_miss->can_read() && pmu_frontend_l2_miss->read()) {
            ev_pmu(ev_num(archx::PMU::STALL_FRONTEND_CACHE));
            ev_pmu(ev_num(archx::PMU::STALL_FRONTEND_MEM));
            ev_pmu(ev_num(archx::PMU::STALL_FRONTEND_MEMBOUND));
        } else if (pmu_frontend_cache_miss->can_read() && pmu_frontend_cache_miss->read()) {
            ev_pmu(ev_num(archx::PMU::STALL_FRONTEND_CACHE));
            ev_pmu(ev_num(archx::PMU::STALL_FRONTEND_L1I));
            ev_pmu(ev_num(archx::PMU::STALL_FRONTEND_MEMBOUND));
        } else if (pmu_frontend_tlb_miss->can_read() && pmu_frontend_tlb_miss->read()) {
            ev_pmu(ev_num(archx::PMU::STALL_FRONTEND_TLB));
            ev_pmu(ev_num(archx::PMU::STALL_FRONTEND_MEMBOUND));
        } else if (improved_flow_tda_alita && (in_btc_clear_window || in_btb_clear_window)) {
            ev_pmu(ev_num(archx::PMU::STALL_FRONTEND_FLOW));
            ev_pmu(ev_num(archx::PMU::STALL_FRONTEND_CPUBOUND));
            if (in_btc_clear_window)
                ev_pmu(ev_num(archx::PMU::STALL_FRONTEND_BTC_CLEAR));
            if (in_btb_clear_window)
                ev_pmu(ev_num(archx::PMU::STALL_FRONTEND_BTB_CLEAR));
        } else if (pmu_frontend_mem_stall->can_read() && pmu_frontend_mem_stall->read()) {
            ev_pmu(ev_num(archx::PMU::STALL_FRONTEND_MEMBOUND));
        } else {
            ev_pmu(ev_num(archx::PMU::STALL_FRONTEND_FLOW));
            ev_pmu(ev_num(archx::PMU::STALL_FRONTEND_CPUBOUND));
        }
    }

    // If rename (DR04) has no uops, then it's a front-end stall
    if (rename_empty) {
        ev_frontend_stall();
        frontend_stall->put(1);
    }

    // Do not emit frontend bandwidth events until after the frontend would have had a chance to deliver
    // the first instructions to the IQ following a flush. This event is used to study/detect frontend
    // steady-state bandwidth problems and including cycles in the shadow of all flushes (including RAW
    // scrubs, etc.) could confuse that.
    if (rename_empty && get_cycle() - flush_cycle >= 3)
        ev_fe_bw(inst_count(0));
}

/* This function reads the throttle bits coming from IXU and FSU and
 * determines a course of action for decode width.
 */
void IDR::throttle_check()
{
    auto width = width_throttle->width();
    if (current_decode_width != width) {
        current_decode_width = width;
        ev_width_switch(ev_decode_width(width));
    }
}

std::deque<Uop> IDR::create_uops(const std::vector<Instruction>& instrs, UopSet& archx_uops, FusionType fusion_type)
{
    bool first_mem_uop{true};
    VirtAddr last_virt_addr{0}; // for communicating VA from STA to STD
    unsigned multiple_memops{0};
    std::deque<Uop> uops;
    int uop_number{0};
    const bool movprfx_unpredicated = (fusion_type == FusionType::MOVPRFX_UNPREDICATED);
    const bool movprfx_predicated = (fusion_type == FusionType::MOVPRFX_PREDICATED);
    const bool movprfx_fusion = movprfx_unpredicated || movprfx_predicated;
    archx::LReg movprfx_src{archx::LReg::INVALID}; // so we can replace operands in 2nd instr
    bool movprfx_zeroing{false};

    for (auto& auop : archx_uops) {
        if (movprfx_fusion && movprfx_src == archx::LReg::INVALID) {
            // do not include the movprfx uop in the core's pan_uops
            movprfx_src = auop->src2;
            AMPERE_ASSERT(instrs[0]->archx_instruction->is_movprfx_unpredicated()
                              || instrs[0]->archx_instruction->is_movprfx_predicated(),
                          "Must be droping a movprfx instruction");
            AMPERE_ASSERT(auop->src2 != archx::LReg::INVALID, "the movprfx src must be valid");
            if (movprfx_predicated) {
                // grab M bit from MOVPRFX/Pg encoding
                movprfx_zeroing = adis::fldfrom_movprfx_z_p_z_(instrs[0]->opcode(), adis::fields_t::M) ? false : true;
            }
            continue;
        }

        pipe_enables_t pipe_enables{a_uops_on_j_pipe,
                                    b_uops_on_j_pipe,
                                    ab_uops_on_j_pipe,
                                    a_uops_on_k_pipe,
                                    b_uops_on_k_pipe,
                                    ab_uops_on_k_pipe,
                                    loads_on_e_pipe,
                                    stores_on_f_pipe,
                                    load_pair_c_only,
                                    u_pipe_enable,
                                    v_pipe_enable,
                                    w_pipe_enable,
                                    x_pipe_by_enable,
                                    y_pipe_by_enable};

        auto uop = make_child<Uop>(instrs[0], std::move(auop), instrs, pipe_enables);

        uop->fused_with_unconditional_branch = instrs[0]->fused_with_unconditional_branch;
        uop->fusion_type = fusion_type;
        uop->uop_number = uop_number++;

        // Assign the virtual address if this is a memory uop.
        if (uop->is_gather() || uop->is_scatter()) {
            set_memop(instrs[0]->archx_instruction, uop, multiple_memops);
            multiple_memops++;
        } else if (uop->is_predicated()) {
            if (uop->is_load() || uop->is_store_address()) {
                // predicated {ld,st}{1,2,3,4}
                // We supply more information in traces for SVE predicated memops.
                set_memop(instrs[0]->archx_instruction, uop, multiple_memops);
                multiple_memops++;
            }
        } else if (uop->is_load() || uop->is_store()) {
            // How to compute the address:
            // 1) A store_data gets the address of the preceding store_address
            // 2) The first memory uop of the instruction gets the instruction's VA from the stream
            // 3) Subsequent memory uops get the instruction's VA plus an offset from the immediate,
            //    which the decoder has set up.
            if (uop->is_store_data()) {
                uop->virt_addr = last_virt_addr;
            } else if (first_mem_uop) {
                uop->virt_addr = instrs[0]->archx_instruction->virtual_address();
            } else {
                AMPERE_ASSERT(
                    uop->archx_uop->ldp_2nd_uop || uop->archx_uop->imm != 0 ||
                        // hack to get past atomic uop in new traces
                        uop->is_atomic(),
                    "Memory uops which are not the first uop in an instruction should have a non-zero immediate");
                uop->virt_addr = instrs[0]->archx_instruction->virtual_address() + uop->archx_uop->imm;
            }

            last_virt_addr = *uop->virt_addr;
        }

        if (uop->is_load() || uop->is_store()) {
            // Tag the first memory uop of an instruction to tell the translation checker in GPC to check it
            // as well as differentiating between multiple loads for the same PC (prefetchers)
            uop->first_mem_uop = first_mem_uop;
            first_mem_uop = false;
        }

        uops.emplace_back(std::move(uop));
    }

    // Record in each uop the number of uops in the instruction it belongs to (panthera instrumentation only)
    for (const auto& uop : uops)
        uop->instruction_uops = uop_number;

    if (movprfx_fusion) {
        const auto& uop = uops.front();
        // replace the first src/dst operand with the movprfx source reg
        for (auto& src_idx : uop->reg_src) {
            if (uop->reg_dst == src_idx) {
                // for unary, binary or trinary ops: this replaces a
                // computational src/dst operand.

                // for zero-ary or unary ops without src/dst operand,
                // this replaces the background data operand. See below.

                // Connor is defining new SVE uops to separate the
                // background data operand from the computational
                // operand.
                src_idx = movprfx_src;
                break;
            }
        }
        if (movprfx_predicated && movprfx_zeroing) {
            if (uop->ainst()->vector_sources() > 1) {
                // do not drop the dependence in zero-ary or unary
                // cases. We preserve false dependence to avoid
                // breaking ARM ISA register-ordering rules.

                //  If we are zeroing, the original dest is
                //  irrelevant.  To avoiding violating the register
                //  ordering constraint in the memory model, we need
                //  to preserve that false dependence on the movprfx
                //  src for zero-ary operations or for unary operations
                //  without a dst/src operand.

                // the merge_src_pos is the yaml-defined operand that
                // provides the background data.
                const unsigned merge_src_pos = uop->uop_info()->sve_merge_source;
                if (merge_src_pos) {
                    // when zeroing movprfx fusion, remove the dependence
                    // on the merge source operand
                    uop->reg_src[merge_src_pos - 1] = archx::LReg::INVALID;
                }
            }
        }
    }

    return uops;
}

// Called when we have determined that it would be possible to fuse the next two instructions,
// this checks whether both of them would be able to fit within the branch/SIR alignment limits.
// This is equivalent to checking whether the resulting fused uop would fit, but we do the
// check based on the instructions to avoid needing to create uops that we won't use (and then
// unwind the side-effects of creating them).
bool IDR::can_fit_fused_uop()
{
    std::vector<Instruction> align_test_instrs{instruction_fifo[0], instruction_fifo[1]};
    bool is_branch = instruction_fifo[0]->is_branch() || instruction_fifo[1]->is_branch();
    return align_limits_test(is_branch, align_test_instrs, false /*emit align reason*/);
}

bool IDR::check_div_msub_fuse(const Instruction& inst1, const Instruction& inst2)
{
    const auto& archx1 = inst1->archx_instruction;
    const auto& archx2 = inst2->archx_instruction;
    if (archx1->is_idiv() && archx2->is_msub()) {
        const auto itext1 = inst1->opcode();
        const auto itext2 = inst2->opcode();
        if (((itext1 ^ itext2) >> 31) == 0) { // operation width (32b/64b) sf field (bit31) match -- same size
            // same field positions for 32/64b gpr {S,U}DIV
            auto div_rd = adis::fldfrom_SDIV_32_dp_2src(itext1, adis::fields_t::Rd);
            auto div_rn = adis::fldfrom_SDIV_32_dp_2src(itext1, adis::fields_t::Rn);
            auto div_rm = adis::fldfrom_SDIV_32_dp_2src(itext1, adis::fields_t::Rm);

            // same field positions for 32/64b MSUB
            auto msub_rd = adis::fldfrom_MSUB_32A_dp_3src(itext2, adis::fields_t::Rd);
            auto msub_rn = adis::fldfrom_MSUB_32A_dp_3src(itext2, adis::fields_t::Rn);
            auto msub_rm = adis::fldfrom_MSUB_32A_dp_3src(itext2, adis::fields_t::Rm);
            auto msub_ra = adis::fldfrom_MSUB_32A_dp_3src(itext2, adis::fields_t::Ra);

            if (div_rd == msub_rd && // gcc compilers seem to generate this one
                div_rd == msub_rn && div_rn == msub_ra && div_rm == msub_rm)
            {
                return true;
            } else if (div_rd == msub_rd && // swapped multiplier version
                       div_rd == msub_rm && div_rn == msub_ra && div_rm == msub_rn)
            {
                return true;
            }
        }
    }
    return false;
}

/* This function returns a pair of Instructions and its associated uops.
 * In fusion cases multiple instructions are returned. */
std::deque<Uop> IDR::decode_instructions(unsigned max_instructions)
{
    std::vector<Instruction> instructions;

#if defined(DEBUG_DECODE)
    if (instruction_fifo.size() > 1) {
        auto& inst = instruction_fifo[0]->archx_instruction;
        auto id = inst->id();
        fmt::print(stderr, "DEC id: {:6} opc: {:08x} {:30s}\n", id, inst->opcode(), inst->disasm());
    }
#endif

    // Two way fusion decode
    if (fusion_enabled && max_instructions > 1 && instruction_fifo.size() > 1) {
        std::uint32_t instr_array[2];
        instr_array[0] = instruction_fifo[0]->archx_instruction->opcode();
        instr_array[1] = instruction_fifo[1]->archx_instruction->opcode();

        // Furiosa does not support fusion between instructions that can fault
        // so we will not ever fuse wrong and right path instructions.  This is
        // an artifact with how we collect instruction streams.  Exclude wrong path
        std::pair<archx::FusionType, archx::UopSet> fusion_uops;

        // 1. We are allowing correct path instr to fuse with other correct path instr. This is obvious.
        // 2. We are allowing wrong path instr to fuse with other wrong path instr (except if either are artificial
        // encodings).
        //    This is new since we started doing wrong path emulation.
        // 3. We allow fusing correct path with wrong path instr but it will never happen
        //    as we do not allow branches as the first instr of a fusing pair.
        if (!instruction_fifo[0]->archx_instruction->artificial_encoding()
            && !instruction_fifo[1]->archx_instruction->artificial_encoding() &&
            // This is an artifact of tracing: We cannot undo a fusion with a wrong-path op that gets flushed.
            // Real hardware would retire the fused pair and then take an exception. But because we are trace
            // based we gotta skip this case an follow the trace.
            !instruction_fifo[0]->requires_flush() && !instruction_fifo[1]->requires_flush())
        {
#if defined(DEBUG_IDR_FUSION)
            for (unsigned jj = 0; jj < 2; ++jj) {
                DBGFUS("inst{} inum: {} wp: {} fake: {} pc: {:016x} disas: {}",
                       jj,
                       instruction_fifo[jj]->archx_instruction->inum().id(),
                       instruction_fifo[jj]->archx_instruction->wrong_path(),
                       instruction_fifo[jj]->archx_instruction->artificial_encoding(),
                       instruction_fifo[jj]->archx_instruction->program_counter().value(),
                       instruction_fifo[jj]->archx_instruction->disasm());
            }
#endif
            fusion_uops = archx::Instruction::decode_fusion(instr_array, fusion_skip_checks, get_decoder_mode());
        }

        // We have fusion
        if (!fusion_uops.second.empty()) {
            bool do_fusion = false;
            if (fusion_uops.first == archx::FusionType::ADR && fusion_adr)
                do_fusion = true;
            else if (fusion_uops.first == archx::FusionType::AES && fusion_aes)
                do_fusion = true;
            else if (fusion_uops.first == archx::FusionType::BC && fusion_bc)
                do_fusion = true;
            else if (fusion_uops.first == archx::FusionType::MOV_MOV && fusion_mov_mov)
                do_fusion = true;
            else if (fusion_uops.first == archx::FusionType::MOV_MOV_TRINITY && fusion_mov_mov_trinity)
                do_fusion = true;
            else if (fusion_uops.first == archx::FusionType::CBZ && fusion_cbz)
                do_fusion = true;
            else if (fusion_uops.first == archx::FusionType::ADDP1 && fusion_addp1)
                do_fusion = true;
            else if (fusion_uops.first == archx::FusionType::ADDSUB_IMM24 && fusion_addsub_imm24)
                do_fusion = true;
            else if (fusion_uops.first == archx::FusionType::ADDSSUBS_CSET && fusion_addssubs_cset)
                do_fusion = true;
            else {
                if (fusion_other) {
                    do_fusion = true;
                } else if (!fusion_ignore_unknown) {
                    AMPERE_FATAL("Unknown fusion detected");
                }
            }

            if (do_fusion) {
                if (can_fit_fused_uop()) {
                    instructions.push_back(instruction_fifo[0]);
                    instructions.push_back(instruction_fifo[1]);
#if defined(DEBUG_IDR_FUSION)
                    auto& inst1 = instruction_fifo[0]->archx_instruction;
                    auto& inst2 = instruction_fifo[1]->archx_instruction;
                    fmt::print(stderr,
                               "FUSING id: {:6}+{:6} opc: {:08x}+{:08x} {:30s}+{:30s}\n",
                               inst1->inum().id(),
                               inst2->inum().id(),
                               inst1->opcode(),
                               inst2->opcode(),
                               inst1->disasm(),
                               inst2->disasm());
#endif

                    return create_uops(instructions, fusion_uops.second, fusion_uops.first);
                } else {
                    emit_align_reason(IDRAlignReason::DecoderFusionDoesNotFit);
                }
            }
        }
    }

    // Regular Instruction decode
    if (max_instructions > 0 && !instruction_fifo.empty()) {
        instructions.push_back(instruction_fifo.front());
        archx::UopSet archx_uops = instructions[0]->archx_instruction->decode(get_decoder_mode());
        AMPERE_ASSERT(
            !archx_uops.empty(), "instruction 0x{:08x} did not decode", instructions[0]->archx_instruction->opcode());

        // If we didn't do one of the fusions above, check whether we can fuse a direct unconditional branch or
        // a nop to the previous instruction. We also handle SVE MOVPRFX fusion and idiv+msub (mod) fusion.
        // We can't do this if:
        // - either instruction will flush, because it confuses GPC
        // - the first instruction is a branch, because then the second instruction may not be on the correct path
        // - the first instruction decodes to more than one uop (except for idiv+msub (mod) fusion)
        // - the first instruction is isolated

        auto fusion_type = FusionType::INVALID;
        if (fusion_enabled && max_instructions > 1 && instruction_fifo.size() > 1
            && !instruction_fifo[0]->requires_flush() && !instruction_fifo[1]->requires_flush()
            && !archx_uops[0]->isolate)
        {
            bool fuse_direct_branch = false;
            bool fuse_nop = false;
            bool fuse_movprfx = false;
            bool fuse_div_msub = false;

            if (archx_uops.size() == 1) {
                if (fusion_direct_branch
                    && instruction_fifo[0]->archx_instruction->branch_type() == archx::BranchType::NONE &&
                    // limit branch fusion to instr that do not use PC (excludes: ADR, ADRP*, PC-rel LDR/PRFM)
                    !archx_uops.back()->use_pc
                    && instruction_fifo[1]->archx_instruction->branch_type() == archx::BranchType::UNCONDITIONAL_DIRECT)
                {
                    fuse_direct_branch = true;
                    fusion_type = FusionType::B_UNCOND;
                } else if (fusion_nop
                           && instruction_fifo[0]->archx_instruction->branch_type() == archx::BranchType::NONE
                           && instruction_fifo[1]->archx_instruction->is_nop())
                {
                    fuse_nop = true;
                    fusion_type = FusionType::NOP;
                }
                // Blind checking of movprfx fusion.
                // Need to adjust sources for 2nd instruction when creating panthera uops
                else if (fusion_movprfx_enable)
                {
                    if (instruction_fifo[0]->archx_instruction->is_movprfx_unpredicated()
                        && instruction_fifo[1]->can_fuse_movprfx_unpredicated())
                    {
                        fuse_movprfx = true;
                        fusion_type = FusionType::MOVPRFX_UNPREDICATED;
                    } else if (instruction_fifo[0]->archx_instruction->is_movprfx_predicated()
                               && instruction_fifo[1]->can_fuse_movprfx_predicated())
                    {
                        fuse_movprfx = true;
                        fusion_type = FusionType::MOVPRFX_PREDICATED;
                    }

                    if (fuse_movprfx) {
                        // get uops for 2nd instruction and add them to the movprfx uop
                        archx::UopSet archx_uops2 = instruction_fifo[1]->archx_instruction->decode(get_decoder_mode());
                        for (auto& auop : archx_uops2) {
                            archx_uops.emplace_back(std::move(auop));
                        }
                    }
                }
            } else if (fusion_div_msub_enable) {
                // If we successfully fuse a gpr idiv which is 2 or 3
                // uops with an msub (2 uops) we drop the msub uops
                // entirely. The fusable combination creates a modulus
                // operation. In a real implementation, we would add
                // information to the div uop or change it to a new
                // uop to denote its modified (modulus) behavior.
                fuse_div_msub = check_div_msub_fuse(instruction_fifo[0], instruction_fifo[1]);
                if (fuse_div_msub) {
                    fusion_type = FusionType::DIV_MSUB;
                }
            }

            if (fuse_direct_branch || fuse_nop || fuse_movprfx || fuse_div_msub) {
                if (can_fit_fused_uop()) {
                    // If we are fusing with a branch (and not a NOP), mark this
                    // instruction as such so that we can be sure to later treat
                    // this uop as a branch and allocate a BOB ID
                    instruction_fifo[0]->fused_with_unconditional_branch = fuse_direct_branch;
                    instructions.push_back(instruction_fifo[1]); // adds 2nd instr to vector
                    archx_uops.back()->end_inst += 1;            // how many instructions does this uop complete (0,1,2)
                } else {
                    emit_align_reason(IDRAlignReason::AutoFusionDoesNotFit);
                }
            }
        }

        // decode stats is a map of decode string to {opcode, num_uops, count}
        if (decode_stats_collection && instructions[0]->archx_instruction->wrong_path() == false) {
            uint32_t opcode = instructions[0]->archx_instruction->opcode();
            std::string source = instructions[0]->archx_instruction->decode_source();
            if (decode_stats.contains(source)) {
                std::get<2>(decode_stats[source])++;
            } else {
                decode_stats[source] = std::make_tuple(opcode, archx_uops.size(), 1);
            }

            for (auto& uop : archx_uops) {
                if (uopcode_stats.contains(uop->opcode)) {
                    uopcode_stats[uop->opcode] += 1;
                } else {
                    uopcode_stats.emplace(uop->opcode, 1);
                }
            }
        }

        return create_uops(instructions, archx_uops, fusion_type);
    }

    return {};
}

bool IDR::uop_goes_to_exec(const Uop& uop) const
{
    // Uop will be sent to execution unless it is an IDR-executed uop or a move that
    // is being eliminated.
    return ((!uop->is_executed_in_idr() || execute_idr_uops) && !uop->is_eliminated_move
            && !uop->archx_uop->ldp_2nd_uop);
}

void IDR::check_move_elimination(UopGroup& uop_row)
{
    uint64_t temp_spec_table[util::enum_max_numeric_size<archx::LReg>()];
    uint64_t fake_dest = -1;

    // Let's just do a quick check of whether we have any moves before we waste any time on dependency tracking
    bool might_eliminate{false};
    for (const auto& uop : uop_row.uops) {
        if ((enable_move_elim_int_zero && uop->is_int_move_zero())
            || (enable_move_elim_int_reg && uop->is_int_move_reg())
            || (enable_move_elim_vec_reg && uop->is_vec_move_reg()))
        {
            might_eliminate = true;
            break;
        }
    }
    if (!might_eliminate)
        return;

    // The counter of how many Lregs point to a Preg is based on the speculative count, meaning at the time of
    // allocation. This means that one uop in the group can affect whether or not a later move in the same group can be
    // eliminated. To get the right answer, we need track the counters as if we were dispatching each uop in order.
    // Making a copy of the whole speculative table isn't great for panthera performance, but it's the simplest way to
    // keep track.
    //
    // A normal uop which writes a dest will get allocated a new PRF entry when it dispatches, but we don't know the
    // real ID of that Preg yet. The fake_dest is used to give a unique ID to all the dests written in this group, which
    // allows us to correctly count the number of Lregs pointing at a Preg. For example, consider this uop group:
    //
    //     add   x20, x20, x24   # x20 is pointed to fake_dest of -1
    //     mov   x24, x20        # can be eliminated -- now x24 points to x20, so x20's Preg has 2 things pointing to it
    //     (x20 and x24) add   x20, x20, x24   # x20 is pointed to fake_dest of -2, so now there is only one Lreg
    //     pointing to each Preg mov   x24, x20        # can also be eliminated, because the previous uop overwrote x20
    //
    for (size_t i = 0; i < util::enum_max_numeric_size<archx::LReg>(); i++)
        temp_spec_table[i] = spec_rf_table[i];

    for (auto& uop : uop_row.uops) {
        // The XLAT will put the register source in src2 of the move_reg uop, or movprfx_d_uv.
        const unsigned move_src_idx = 1; // src2 = index 1. which src to read
        const archx::LReg move_src = uop->reg_src[move_src_idx];

        // There's no limit on the number of registers which can be zero, so no need for further checks
        if (enable_move_elim_int_zero && uop->is_int_move_zero()) {
            temp_spec_table[uop->reg_dst] = temp_spec_table[move_src];
            uop->is_eliminated_move = true;
            continue;
        }

        // For anything which is not a move, just note which Lregs it is overwriting and move on
        if (!((enable_move_elim_int_reg && uop->is_int_move_reg())
              || (enable_move_elim_vec_reg && uop->is_vec_move_reg())))
        {
            if (move_elim_same_group) {
                if (uop->is_producer())
                    temp_spec_table[uop->reg_dst] = fake_dest--;
                if (uop->has_dest2())
                    temp_spec_table[uop->reg_dst2] = fake_dest--;
            }
            continue;
        }

        AMPERE_ASSERT(!invalid_reg(move_src), "Eliminated move had an invalid source");

        // If the source of this move has been zeroed by a previous move, then it can be eliminated
        // since no PRF counter is required (the 'is zero' bit will just be transferred from src to dest)
        if (uop->is_int_move_reg()
            && (temp_spec_table[move_src] == temp_spec_table[archx::LReg::ZR]
                || temp_spec_table[move_src] == temp_spec_table[archx::LReg::INVALID]))
        {
            temp_spec_table[uop->reg_dst] = temp_spec_table[move_src];
            uop->is_eliminated_move = true;
            continue;
        }

        // Count how many Lregs are pointing to the Preg which is the source of the move
        uint64_t src_counter{0};
        for (unsigned long i : temp_spec_table)
            if (i == temp_spec_table[move_src])
                src_counter++;

        // If there are already the maximum number of Lregs pointing to the Preg which is the source of the move, then
        // it can't be eliminated. Treat it like a normal uop: just note that it overwrites its dest.
        if (src_counter >= move_elim_max_counter) {
            ev_elim_ctr_overflow(uop);
            if (move_elim_same_group)
                temp_spec_table[uop->reg_dst] = fake_dest--;
            continue;
        }

        // Uop can be eliminated, so point its dest to its source
        temp_spec_table[uop->reg_dst] = temp_spec_table[move_src];
        uop->is_eliminated_move = true;
    }
}

void IDR::count_resources(UopGroup& uop_row)
{
    if (uop_row.resource_count_complete) {
        // For most resources, what's needed by the uop group is based purely on the uop and can't change, so we
        // only count those once. But IDR re-evaluates pipe assignment every cycle, which means the schedulers needed
        // by the group might change while we are stalled.
        //
        // So even if we have already counted resources for this group, re-count the schedulers needed.
        for (unsigned i = 0; i < sch.size(); i++)
            uop_row.resource_count[i] = 0;

        for (auto& uop : uop_row.uops) {
            if (uop_goes_to_exec(uop))
                uop_row.resource_count[uop->sch.scheduler()]++;
        }
        return;
    }

    // First figure out if any of the uops in this group are moves which will be eliminated, so
    // that we know whether they need a PRF entry for their dest, and a scheduler credit.
    check_move_elimination(uop_row);

    for (auto& uop : uop_row.uops) {
        // register file for primary dest
        if (uop->is_producer() && !uop->is_eliminated_move) {
            if (archx::is_vector(uop->reg_dst))
                uop_row.resource_count[buffer_resource_id(BufferResource::FP_RF)]++;
            else if (archx::is_integer(uop->reg_dst))
                uop_row.resource_count[buffer_resource_id(BufferResource::INT_RF)]++;
            else if (archx::is_predicate(uop->reg_dst))
                uop_row.resource_count[buffer_resource_id(BufferResource::PRED_RF)]++;
            else
                AMPERE_ERROR("dest not vector, integer, or predicate");
        }

        // The second uop in LDP only allocates a PRF entry, no other resources -- it will
        // share the LOB and ROB entry with the first uop.
        if (uop->archx_uop->ldp_2nd_uop)
            continue;

        // register file for second dest (currently only used for vector load-pair)
        if (uop->has_dest2()) {
            if (archx::is_vector(uop->reg_dst2))
                uop_row.resource_count[buffer_resource_id(BufferResource::FP_RF)]++;
            else if (archx::is_integer(uop->reg_dst2))
                uop_row.resource_count[buffer_resource_id(BufferResource::INT_RF)]++;
            else
                AMPERE_ERROR("dest2 is neither vector nor integer");
        }

        if (uop->is_gather()) {
            uop_row.resource_count[buffer_resource_id(BufferResource::GATHER_TBL)]++;
        }

        // rob
        if (!uop->cleaved_first_half)
            uop_row.resource_count[buffer_resource_id(BufferResource::ROB)]++;

        // Scheduler
        if (uop_goes_to_exec(uop))
            uop_row.resource_count[uop->sch.scheduler()]++;

        // flags
        if (uop->is_flag_producer() || uop->is_temp_flags_producer())
            uop_row.resource_count[buffer_resource_id(BufferResource::FLAG_RF)]++;

        // lob
        if (uop->is_load())
            uop_row.resource_count[buffer_resource_id(BufferResource::LOB)]++;

        // sob
        if (uop->is_store_address())
            uop_row.resource_count[buffer_resource_id(BufferResource::SOB)]++;

        // bob
        if (uop->is_branch())
            uop_row.resource_count[buffer_resource_id(BufferResource::BOB)]++;

        // FSU X/Y pipe
        if (uop->sch.sched_type == Sch::X || uop->sch.sched_type == Sch::Y)
            uop_row.resource_count[sch_bandwidth_resource_id(*uop->sch.sched_type)]++;
    }

    uop_row.resource_count_complete = true;
}

void IDR::record_stalls(UopGroup& uop_row, ResourceVector& resource_vector, std::size_t util, unsigned resource)
{
    auto needed = uop_row.resource_count[resource];

    // Bug: when we backend stall, we wait for 2x the scheduler
    // credits we actually need for this uop group
    if (sched_credit_stall_bug && resource < sch.size() && previously_backend_stalled)
        needed *= 2;

    // zero the unavailable resource and conditionally emit 2 stall events
    if (needed > util) {
        resource_vector[resource] = false;
        if (resource_stats_detailed) {
            if (resource < sch.size())
                ev_scheduler_stall(ev_sch_num(resource));
            else if (resource < (sch.size() + util::enum_size<BufferResource>()))
                ev_resource_stall(ev_buffer_num(static_cast<BufferResource>(resource - sch.size())));
            else
                ev_scheduler_bandwidth_stall(ev_sch_type_num(
                    static_cast<SchedulerType>(resource - sch.size() - util::enum_size<BufferResource>())));
        }
    }
}

IDR::ResourceVector IDR::check_resources(UopGroup& uop_row)
{
    ResourceVector resource_vector;
    resource_vector.set();

    size_t offset = 0;
    for (unsigned i = 0; i < sch.size(); i++) {
        auto util = sch[i].get_count();
        record_stalls(uop_row, resource_vector, util, i);
    }

    offset += sch.size();
    for (auto [i, pool] : std::ranges::views::enumerate(buffer_resource_credit_pools)) {
        auto util = pool->get_count();
        record_stalls(uop_row, resource_vector, util, offset + i);
    }

    if (fsu_sch_bw_limit) {
        offset += util::enum_size<BufferResource>();
        for (unsigned i = offset; i < (offset + util::enum_size<SchedulerType>()); ++i) {
            std::size_t util = 0;
            switch (static_cast<SchedulerType>(i - offset)) {
                // If we dispatched more than 2 uops to either the FSU X or Y pipes in the previous cycle,
                // the IDR-FSU interface will stall for one (current) cycle. As a result, there will be an IDR stall
                // if there is ANY fsu uop in this cycle.
                //
                // These resources only exist to introduce a one-cycle stall, so they are basically infinity or zero.
                // Here we use decode_width * 2 to ensure that even with cleaving we always have enough resources
                // to dispatch the group, if we are not stalled.
            case Sch::X:
                util = (stall_idr_fsu && stall_idr_fsu_pipe == Pipe::X) ? 0 : uop_decode_width * 2;
                break;
            case Sch::Y:
                util = (stall_idr_fsu && stall_idr_fsu_pipe == Pipe::Y) ? 0 : uop_decode_width * 2;
                break;
            default:
                break;
            }
            record_stalls(uop_row, resource_vector, util, i);
        }
    }

    return resource_vector;
}

unsigned IDR::stall_cause(const IDR::ResourceVector& resource_vector)
{
    // Resource vector has 0s for unavailable resources, so invert it and count the least significant 0s
    // to find the first unavailable resource. This builtin is undefined if the input is zero, but that can't
    // happen because no single uop can need every single resource.
    return __builtin_ctzl(~resource_vector.to_ulong());
}

void IDR::assign_resources(const Uop& uop)
{
    // Record the PRF entry which holds the most recent copy of each of the uop's sources,
    // which includes flags. This will be used for dependency tracking in the IXU.
    for (uint src = 0; src < UOP_MAX_SRCS; src++) {
        // Unary ops use their dest reg as src2 (src==1) as conditional source.
        // When FEAT_AFP NEP == 1, src2 should be considered. (not expected to ever be used).
        // When FEAT_AFP NEP == 0, src2 should be ignored. (common case).
        // We need logic to ignore src2 from the XLAT for normal operation.
        if (enable_NEP == false && src == 1 && uop->nep_dest_as_src()) {
            uop->reg_psrc[src] = 0;
        } else {
            // FIXME: could zero the entry if the lreg is ZR.
            uop->reg_psrc[src] = spec_rf_table[uop->reg_src[src]];
        }
    }

    if (uop->is_producer()) {
        CountedCreditPool* dest_rf{nullptr};

        if (archx::is_vector(uop->reg_dst))
            dest_rf = &rf_fp;
        else if (archx::is_integer(uop->reg_dst))
            dest_rf = &rf_int;
        else if (archx::is_predicate(uop->reg_dst))
            dest_rf = &rf_pred;
        else
            AMPERE_ERROR("dest is neither vector nor integer");

        // If this is a move being eliminated, then its dest now just points to the same PRF
        // entry as its source. Otherwise, allocate a new PRF to hold the dest.
        if (uop->is_eliminated_move) {
            archx::LReg move_src = archx::invalid_reg(uop->reg_src[1]) ? uop->reg_src[0] : uop->reg_src[1];
            uop->prfid = dest_rf->duplicate(spec_rf_table[move_src]);

            if (uop->is_int_move_reg())
                ev_elim_int_reg(uop);
            else if (uop->is_int_move_zero())
                ev_elim_int_zero(uop);
            else if (uop->is_vec_move_reg())
                ev_elim_vec_reg(uop);
            else
                AMPERE_ERROR("Eliminated move of unknown type");
        } else if (uop->is_gather()) {
            // need to reuse dst prfid for gather destinations 2 or 4
            // times.  Since the Credits are unique_ptr objects, we
            // make an indirection object to hold the shared prfid
            // Credit for gathers.

            if (!uop->get_gather_proxy()) {
                DBG_GATHERC("IDR make new gather proxy parent txid: {}, txid: {}, alloc_id: {}",
                            uop->get_txid_parent(),
                            uop->get_txid(),
                            uop->get_alloc_id());
                GatherProxyCreditPtr gprox =
                    std::make_shared<GatherProxyCredit>(uop->get_txid_parent(), uop->get_alloc_id(), uop->is_gather2());
                uop->instructions.back()->gather_proxy = gprox;
                gprox->set_prfid_credit(dest_rf->acquire());
                gprox->set_gather_tbl_credit(gather_tbl.acquire());
            } else {
                DBG_GATHERC("IDR reuse gather proxy parent txid: {}, txid: {}, alloc_id: {}",
                            uop->get_txid_parent(),
                            uop->get_txid(),
                            uop->get_alloc_id());
                GatherProxyCreditPtr gprox = uop->get_gather_proxy();
                gprox->set_alloc_id(uop->get_alloc_id());
            }
            DBG_GATHERC("IDR gather: pc: {:016x} parent txid: {}, txid: {}: dis: {}",
                        uop->pc().value(),
                        uop->get_txid_parent(),
                        uop->get_txid(),
                        *(uop->archx_uop));

        } else {
            uop->prfid = dest_rf->acquire();
        }

        // Update the speculative register table to say that the latest version of this
        // architectural dest is now in the PRF entry associated with this uop.
        {
            uint64_t credit_id{0};
            credit_id = uop->get_prfid_id();
            spec_rf_table[uop->reg_dst] = credit_id;
        }
    }

    // The second uop in LDP only allocates a PRF entry, no other resources -- it will
    // share the LOB and ROB entry with the first uop.
    if (uop->archx_uop->ldp_2nd_uop)
        return;

    // For cleaved pairs, the ROB credit belongs to the second uop, so that we don't
    // release it until both halves have committed.
    uop->rob_entry = next_rob_entry;
    if (!uop->cleaved_first_half) {
        uop->robid = rob.acquire();

        if (++next_rob_entry == rob.get_size())
            next_rob_entry = 0;
    }

    if (uop_goes_to_exec(uop)) {
        uop->schid = sch[uop->sch.scheduler()].acquire();
#if defined(DEBUG_SCHEDULER_CREDITS)
        static const char* pipename[] = {"A0", "B0", "M0", "A1", "B1", "M1", "X", "Y"};
        fmt::print(stderr,
                   "cyc {}: {} acquire schid {} for {:s} at {:x} allocid: {} txid: {}\n",
                   get_cycle(),
                   (uop->sch.sched < NUM_OPSCHED ? pipename[uop->sch.sched] : "UNKN"),
                   uop->schid->get_id(),
                   uop->archx_uop->opcode,
                   uop->pc().value(),
                   uop->get_alloc_id(),
                   uop->get_txid());
#endif
    }
    if (uop->has_dest2()) {
        if (archx::is_vector(uop->reg_dst2))
            uop->prfid2 = rf_fp.acquire();
        else if (archx::is_integer(uop->reg_dst2))
            uop->prfid2 = rf_int.acquire();
        else
            AMPERE_ERROR("dest2 is neither vector nor integer");

        // Update the speculative register table to say that the latest version of this
        // architectural dest is now in the PRF entry associated with this uop.
        spec_rf_table[uop->reg_dst2] = uop->prfid2->get_id();
    }

    if (uop->is_flag_producer()) {
        uop->flagsid = rf_flag.acquire();
        // Update the speculative register table to say that the latest version of the flags
        // is now in the flags PRF entry associated with this uop.
        spec_rf_table[archx::LReg::FLAGS] = uop->flagsid->get_id();
    }
    if (uop->is_temp_flags_producer()) {
        uop->flagsid = rf_flag.acquire();
        spec_rf_table[archx::LReg::FLGT0] = uop->flagsid->get_id();
    }

    if (uop->is_load()) {
        uop->lobid = lob.acquire();
        uop->lob_alloc_id_val = lob_alloc_id_counter++;
        uop->lob_id_val = lob_id_counter;
        lob_id_counter++;
        if (lob_id_counter > (lob.get_size() - 1))
            lob_id_counter = 0; // wrap at size of the LOB
    }
    if (uop->is_store_address()) {
        uop->sobid = sob.acquire();
    }
    if (uop->is_branch()) { // includes fused_with_unconditional_branch
        uop->bobid = bob.acquire();
    }

    // Attach the current SWOB size to the uop so that GPC knows how many writes have to drain on a flush
    uop->swob_occupancy = swob.size();
}

void IDR::iq_credit_return(UopGroup& uop_row)
{
    for (const auto& uop : uop_row.uops) {
        // Deallocate from the instruction fifo
        if (uop->archx_uop->end_inst) {
            for (const auto& inst : uop->instructions) {
                auto fetch = inst->fetch;
                instruction_credit_return->put(std::move(inst->fifoid));
                inst->fifoid.reset();

                if (inst->is_fetch_exit()) {
                    ev_feq_dealloc(fetch, fetch_id_data(fetch->fetch_id));
                    if (fetch->feq_credit)
                        feq_credit_return->put(std::move(fetch->feq_credit));
                }
            }
        }
    }
}

void IDR::dispatch(UopGroup& uop_row)
{
    auto fifo_occupancy = instruction_fifo.size();
    size_t fifo_rightpath_instructions = fifo_occupancy;
    if (fifo_occupancy > 0 && instruction_fifo.back()->wrong_path()) {
        fifo_rightpath_instructions = 0;
        for (auto& instruction : instruction_fifo) {
            if (instruction->wrong_path())
                break;
            fifo_rightpath_instructions++;
        }
    }

    ev_fe_bw(inst_count(uop_row.count_instructions()),
             iq_occupancy(fifo_occupancy),
             iq_rightpath_occupancy(fifo_rightpath_instructions));

    dispatch_pmu(uop_row);
    Uop prev_uop{};
    uint64_t real_uops_dispatched{0};

    if (!dr03_iq_credit_return)
        iq_credit_return(uop_row);

    for (const auto& uop : uop_row.uops) {
        if (uop->is_branch() && uop->bobid)
            bob_alloc->put(uop);

        if (uop->archx_uop->ldp_2nd_uop) {
            prev_uop->reg_dst2 = uop->reg_dst;
            prev_uop->prfid2 = std::move(uop->prfid);
            prev_uop->archx_uop->end_inst = uop->archx_uop->end_inst;
        } else {
            if (uop_goes_to_exec(uop)) {
                if (uop->is_gather()) {
                    DBG_GATHERC(
                        "Sending gather to exec. parent txid: {}, txid: {}", uop->get_txid_parent(), uop->get_txid());
                }
                out->put(uop); // to IXU
            }

            dispatched_uops.push_back(uop);
            gpc_dispatch_port->put(uop);

            // Count the number of non-cleaved uops we dispatch for assertion check below
            if (!uop->cleaved_second_half)
                real_uops_dispatched++;

            // Sends preview of all mem-uops to lsu
            if (uop->is_store() || uop->is_load()) {
                lsu_mem_rob_preview_port->put(uop);

                if (uop->is_store())
                    ev_uop_store_type(uop, store_type_data(static_cast<IDRStoreType>(uop->is_single_uop_store())));
            }
        }
        prev_uop = uop;
    } // for uops in row

    AMPERE_ASSERT(real_uops_dispatched <= current_decode_width,
                  "Number of non-cleaved uops dispatched in a cycle should not exceed the decode width");
}

void IDR::dispatch_pmu(UopGroup& uop_row)
{
    for (auto& uop : uop_row.uops) {
        ev_pmu(uop, ev_num(archx::PMU::OP_SPEC));
        if (uop->cleaved_second_half)
            ev_pmu(uop, ev_num(archx::PMU::OP_CLEAVED_SPEC));
        if (uop->is_isolated())
            ev_pmu(uop, ev_num(archx::PMU::OP_ISOLATED_SPEC));

        if (DECODE_THROTTLE_WIDTH == current_decode_width)
            ev_pmu(ev_num(archx::PMU::IDR_THROTTLE_UOPS_SPEC));

        // ASE_SVE_UOP_SPEC = every uop that is part of an instruction counted by ASE_INST_SPEC or SVE_INST_SPEC
        bool is_ase_uop = false;
        bool is_sve_uop = false;
        bool is_int_inst = false;
        bool is_fp_inst = false;
        bool is_scale_inst = false;
        for (auto& inst : uop->instructions) {
            auto& archx_instr = inst->archx_instruction;
            for (auto pmu_ctr : archx_instr->inst_info().pmu_type) {
                if (pmu_ctr == archx::PMU::ASE_UOP_SPEC)
                    is_ase_uop = true;
                else if (pmu_ctr == archx::PMU::SVE_UOP_SPEC)
                    is_sve_uop = true;
                else if (pmu_ctr == archx::PMU::INT_SPEC)
                    is_int_inst = true;
                else if (pmu_ctr == archx::PMU::FP_SPEC)
                    is_fp_inst = true;
                else if ((pmu_ctr == archx::PMU::SVE_INT_SPEC) || (pmu_ctr == archx::PMU::SVE_FP_SPEC))
                    is_scale_inst = true;
            }
        }

        // FP_FIXED_OPS_SPEC
        // FP_SCALE_OPS_SPEC
        // INT_FIXED_OPS_SPEC
        // INT_SCALE_OPS_SPEC
        // Implementation: These count by *uops* dispatched. By counting per-uop, the effective count per-instructions,
        // as defined by the Arm spec, should add up correctly. The uop-counting is based on the following rules:
        //  - all counts are based on cleavlets (pre-cleaved uops)
        //  - for FSU uops, the uop op_count attribute indicates the op count, so increment based on that
        //  - for IXU uops, we increment by 'one' only if it's the last uop of the instruction
        //  - for multi-uop instructions that have mixed "FSU + IXU" uops, same above rules apply, ie:
        //      - non-instruction-ending IXU uops are ignored
        //      - FSU uops count by the uop op_count attribute, regardless of where in the flow they are, or how many
        //        there are in the flow instruction-ending IXU uops count by 'one'.

        int op_count = 0;
        if (uop->is_fsu())
            op_count = uop->op_count();
        else if (uop->archx_uop->end_inst)
            op_count = 1;

        if (is_int_inst) {
            if (is_scale_inst) {
                for (int i = 0; i < op_count; i++) {
                    ev_pmu(uop, ev_num(archx::PMU::INT_SCALE_OPS_SPEC));
                }
            } else {
                for (int i = 0; i < op_count; i++) {
                    ev_pmu(uop, ev_num(archx::PMU::INT_FIXED_OPS_SPEC));
                }
            }
        }

        if (is_fp_inst) {
            if (is_scale_inst) {
                for (int i = 0; i < op_count; i++) {
                    ev_pmu(uop, ev_num(archx::PMU::FP_SCALE_OPS_SPEC));
                }
            } else {
                for (int i = 0; i < op_count; i++) {
                    ev_pmu(uop, ev_num(archx::PMU::FP_FIXED_OPS_SPEC));
                }
            }
        }

        if (!uop->archx_uop->end_inst) {
            if (is_ase_uop)
                ev_pmu(uop, ev_num(archx::PMU::ASE_UOP_SPEC));
            if (is_sve_uop)
                ev_pmu(uop, ev_num(archx::PMU::SVE_UOP_SPEC));
            if (is_ase_uop || is_sve_uop)
                ev_pmu(uop, ev_num(archx::PMU::ASE_SVE_UOP_SPEC));
        } else {
            // instruction-oriented PMU events are tallied on the
            // last uop in that uop sequence.

            bool is_ase = false;
            bool is_ase_fp = false;
            bool is_sve_fp = false;
            bool is_ase_inst = false;
            bool is_sve_inst = false;
            bool is_ase_ldst = false;
            bool is_sve_ldst = false;
            bool is_ase_uop = false;
            bool is_sve_uop = false;
            for (auto& inst : uop->instructions) {
                ev_pmu(inst, ev_num(archx::PMU::INST_SPEC));
                auto& archx_instr = inst->archx_instruction;

                // inst_info has a vector of pmu values we derived from the XLAT
                // PMU events list is picked up from non-fusion decoders (fusion decoders do not have PMU list/buckets)
                for (auto pmu_ctr : archx_instr->inst_info().pmu_type) {
                    DBG_IPMU("{:s} {:2s} {:016x} {:20s} {:04x} {:30s} {:08x} {:s}",
                             (uop->is_fused()) ? "F" : "U",
                             (uop->wrong_path() ? "WP" : ""),
                             uop->pc().value(),
                             uop->get_uopcode(),
                             static_cast<unsigned>(pmu_ctr),
                             util::enum_to_string(pmu_ctr),
                             archx_instr->opcode(),
                             archx_instr->disasm());
                    ev_pmu(inst, ev_num(pmu_ctr));
                    if (pmu_ctr == archx::PMU::ASE_SPEC)
                        is_ase = true;
                    if (pmu_ctr == archx::PMU::ASE_FP_SPEC)
                        is_ase_fp = true;
                    if (pmu_ctr == archx::PMU::SVE_FP_SPEC)
                        is_sve_fp = true;
                    if (pmu_ctr == archx::PMU::ASE_INST_SPEC)
                        is_ase_inst = true;
                    if (pmu_ctr == archx::PMU::SVE_INST_SPEC)
                        is_sve_inst = true;
                    if (pmu_ctr == archx::PMU::ASE_LDST_SPEC)
                        is_ase_ldst = true;
                    if (pmu_ctr == archx::PMU::SVE_LDST_SPEC)
                        is_sve_ldst = true;
                    if (pmu_ctr == archx::PMU::ASE_UOP_SPEC)
                        is_ase_uop = true;
                    if (pmu_ctr == archx::PMU::SVE_UOP_SPEC)
                        is_sve_uop = true;
                }

                if (is_ase && !is_ase_fp)
                    ev_pmu(inst, ev_num(archx::PMU::ASE_NONFP_SPEC));
                if (is_ase_fp || is_sve_fp)
                    ev_pmu(inst, ev_num(archx::PMU::ASE_SVE_FP_SPEC));
                if (is_ase_inst || is_sve_inst)
                    ev_pmu(inst, ev_num(archx::PMU::ASE_SVE_INST_SPEC));
                if (is_ase_ldst || is_sve_ldst)
                    ev_pmu(inst, ev_num(archx::PMU::ASE_SVE_LDST_SPEC));
                if ((is_ase && !is_ase_fp) || (is_sve_inst && !is_sve_fp))
                    ev_pmu(inst, ev_num(archx::PMU::ASE_SVE_NONFP_SPEC));
                if (is_ase_uop || is_sve_uop)
                    ev_pmu(inst, ev_num(archx::PMU::ASE_SVE_UOP_SPEC));

                if (is_dsb(archx_instr->inst_info().barrier_type))
                    ev_pmu(inst, ev_num(archx::PMU::DSB_SPEC));
                else if (is_dmb(archx_instr->inst_info().barrier_type))
                    ev_pmu(inst, ev_num(archx::PMU::DMB_SPEC));
                else if (is_isb(archx_instr->inst_info().barrier_type))
                    ev_pmu(inst, ev_num(archx::PMU::ISB_SPEC));
                else if (is_csdb(archx_instr->inst_info().barrier_type))
                    ev_pmu(inst, ev_num(archx::PMU::CSDB_SPEC));

                if (archx_instr->inst_info().is_acquire)
                    ev_pmu(inst, ev_num(archx::PMU::RC_LD_SPEC));
                if (archx_instr->inst_info().is_release)
                    ev_pmu(inst, ev_num(archx::PMU::RC_ST_SPEC));
                if (archx_instr->is_branch()) {
                    auto branch_type = archx_instr->branch_type();

                    ev_pmu(inst, ev_num(archx::PMU::PC_WRITE_SPEC));

                    if (branch_type != BranchType::UNCONDITIONAL_DIRECT)
                        ev_pmu(inst, ev_num(archx::PMU::BR_PRED));

                    if (archx::is_return(branch_type))
                        ev_pmu(inst, ev_num(archx::PMU::BR_RETURN_SPEC));
                    if (archx::is_direct(branch_type))
                        ev_pmu(inst, ev_num(archx::PMU::BR_IMMED_SPEC));
                    if (archx::is_indirect(branch_type))
                        ev_pmu(inst, ev_num(archx::PMU::BR_INDIRECT_SPEC));
                }
                if (archx_instr->is_eret()) {
                    ev_pmu(inst, ev_num(archx::PMU::PC_WRITE_SPEC));
                } else if (archx_instr->is_tlbi()) {
                    ev_pmu(inst, ev_num(archx::PMU::TLBI_SPEC));
                } else if (archx_instr->is_cache_maint()) {
                    ev_pmu(inst, ev_num(archx::PMU::CMO_SPEC));
                }
            } // for instructions
        } // if end inst
    } // for uops in row
}

void IDR::check_self_sync(UopGroup& uop_row) const
{
    for (auto& uop : uop_row.uops) {
        if (uop->is_mtspr()) {
            switch (uop->archx_uop->imm) {
            // ELR and SPSR writes are self-synchronizing in Furiosa/Ripley
            case 0xc201: // ELR_EL1
            case 0xea01: // ELR_EL12
            case 0xe201: // ELR_EL2
            case 0xc200: // SPSR_EL1
            case 0xea00: // SPSR_EL12
            case 0xe200: // SPSR_EL2
                if (!improved_spr_behavior)
                    uop->self_sync = true;
                break;
            // DAIF with a register source is self-synchronizing
            case 0xda11: // DAIF
                if (!archx::invalid_reg(uop->archx_uop->src2))
                    uop->self_sync = true;
                break;
            // Writes to the following SPR/SRs are self-synchronizing
            case 0xda20: // FPCR
            case 0xda21: // FPSR
            case 0xc214: // UAO
            case 0xc2b1: // TFSRE0_EL1
            case 0xc2b0: // TFSR_EL1
            case 0xe2b0: // TFSR_EL2
            case 0xf2b0: // TFSR_EL3
            case 0xda29: // DLR_EL0
            case 0xda28: // DSPSR_EL0
            case 0xdce5: // PMSELR_EL0
            case 0xc3a0: // PAR_EL1
            case 0xc230: // ICC_PMR_EL1
            case 0xcfe5: // ICV_PMR_EL1
            case 0xe088: // HCR_EL2
            case 0xf088: // SCR_EL3
            case 0xc080: // SCTLR_EL1
            case 0xe080: // SCTLR_EL2
            case 0xf080: // SCTLR_EL3
            case 0xf099: // MDCR_EL3
            case 0xc102: // TCR_EL1
            case 0xe102: // TCR_EL2
            case 0xf102: // TCR_EL3
            case 0xe293: // VSESR_EL2
            case 0xf7e2: // IMP_BRBE_IALL_EL3
            case 0xf7e3: // IMP_BRBE_INJ_EL3
                uop->self_sync = true;
                break;
            default:
                break;
            }
        }
    }
}

bool IDR::update_swob(UopGroup& uop_group)
{
    for (const auto& uop : uop_group.uops) {
        if (uop->is_mtspr()) {
            if (swob.empty()) {
                swob_oldest = get_cycle();
            } else if (swob.size() >= swob_size) { // SWOB full, flush swob
                ev_pmu(uop, ev_num(archx::PMU::IDR_STALL_SWOB_FULL));
                return true;
            }

            swob.push_back(uop);
        }

        // SWOB timeout: stall and signal a flush at the next convenient boundary. This has to be at an instruction
        // boundary because in panthera, like in real life, Bad Things Happen if you flush in the middle of an
        // instruction. Avoiding branches and uops which will flush for other reasons is just to avoid nasty
        // corner cases -- this flush doesn't need to taken at precisely the right time.
        if (swob_drain_timeout_enable) {
            if (!swob.empty() && (get_cycle() - swob_oldest >= swob_drain_timeout)
                && uop_group.uops[0]->archx_uop->start_inst && !uop_group.uops[0]->is_branch()
                && !uop_group.uops[0]->instructions[0]->requires_flush())
            {
                ev_pmu(uop, ev_num(archx::PMU::IDR_STALL_SWOB_TIMEOUT));
                return true;
            }
        }

        // Check for SWOB RAW
        if (uop->is_mfspr() && !swob.empty()) {
            bool flush_swob = true;

            if (swob_raw_cam_check_enable) {
                flush_swob = false;
                for (auto& swob_uop : swob) {
                    if (swob_uop->archx_uop->imm == uop->archx_uop->imm) {
                        flush_swob = true;
                        break;
                    } else {
                        // Check for aliases
                        switch (swob_uop->archx_uop->imm) {
                        case 0xc201: // ELR_EL1
                        case 0xea01: // ELR_EL12
                        case 0xe201: // ELR_EL2
                            if ((uop->archx_uop->imm == 0xc201) || (uop->archx_uop->imm == 0xea01)
                                || (uop->archx_uop->imm == 0xe201))
                                flush_swob = true;
                            break;
                        case 0xc200: // SPSR_EL1
                        case 0xea00: // SPSR_EL12
                        case 0xe200: // SPSR_EL2
                            if ((uop->archx_uop->imm == 0xc200) || (uop->archx_uop->imm == 0xea00)
                                || (uop->archx_uop->imm == 0xe200))
                                flush_swob = true;
                            break;
                        default:
                            break;
                        }
                        if (flush_swob)
                            break;
                    }
                }
            }

            if (flush_swob) {
                ev_pmu(uop, ev_num(archx::PMU::IDR_STALL_SWOB_RAW));
                return true;
            }
        }
    }

    return false;
}

void IDR::assign_alloc_ids(UopGroup& uop_row)
{
    for (auto& uop : uop_row.uops) {
        if (uop->archx_uop->ldp_2nd_uop)
            continue;

        uop->set_alloc_id(++alloc_id_counter);
        ev_uop_dispatch(uop, alloc_id(uop->get_alloc_id()));

        if (uop->is_store_address() || uop->is_atomic() || uop->is_store_special())
            sta_uop_id = uop->get_alloc_id();

        uop->store_associated_sta_id = sta_uop_id;
    }
}

std::string IDR::disassemble_uop(const Uop& uop)
{
    // use the core uop disasm instead of archx uop disasm because SVE
    // movprfx fusion changes the operands.
    std::ostringstream os;
    os << uop->archx_uop->opcode << " ";
    if (uop->reg_dst != archx::LReg::INVALID) {
        os << uop->reg_dst;
    }
    if (uop->reg_dst2 != archx::LReg::INVALID) {
        os << ", " << uop->reg_dst2;
    }
    os << " <- ";
    bool first = true;
    for (const auto& src : uop->reg_src) {
        if (src != archx::LReg::INVALID) {
            if (!first) {
                os << ", ";
            }
            os << src;
            first = false;
        }
    }
    return os.str();
}

std::string IDR::allowed_pipes(const Uop& uop) const
{
    std::string ret;
    auto allowed_pipes = uop->allowed_pipes();

    for (auto pipe : util::each_enum<Uoppipe>())
        if (allowed_pipes & pipe)
            ret += util::enum_to_string(pipe);

    return ret;
}

void IDR::assign_sch(const Uop& uop, Scheduler sch)
{
    assign_sch(uop, *sch.sched_type, sch.partition);
}

void IDR::assign_sch(const Uop& uop, SchedulerType sched_type, std::optional<unsigned> partition)
{
    uop->sch.sched_type = sched_type;
    uop->sch.partition = partition.value_or(uop->decode.partition);
    uop->sch.normalize();
    uop->set_pipe_type(pipe_from_sch(sched_type, uop->allowed_pipes()));
    AMPERE_ASSERT_ERROR(uop->sch.pipe_type != Pipe::INVALID, "Invalid scheduler for uop allowed pipes");
    sch_assigned_this_cycle[uop->sch.scheduler()]++;
}

void IDR::assign_schedulers(const std::vector<Uop>& uops, const std::vector<std::optional<Scheduler>>& schedulers)
{
    for (auto [uop, sched] : std::views::zip(uops, schedulers) | std::views::as_const) {
        if (sched)
            assign_sch(uop, *sched);
        else
            assign_pipe(uop); // no scheduler picked by assignment policy, use least loaded
    }
}

std::optional<Scheduler> IDR::pipe_balance(const Uop& uop)
{
    if (uop->archx_uop->ldp_2nd_uop)
        return {};

    auto allowed_pipes = uop->allowed_pipes();
    auto allowed_schedulers = Scheduler::eligible_scheduler_types(allowed_pipes);
    std::vector<Scheduler> schedulers;

    if (cross_pipe) {
        schedulers.reserve(allowed_schedulers.size() * MAX_PARTITIONS);
        for (auto sch : allowed_schedulers) {
            unsigned partitions = is_fsu_op(sch) ? fsu_partitions : IXU_PARTITIONS;
            for (unsigned i = 0; i < partitions; ++i)
                schedulers.emplace_back(sch, i);
        }

        return least_loaded_sch(schedulers, uop->decode.partition, uop->decode.lane);
    }

    // Select least loaded schedulers for the one that matches the allowed pipes
    scheduler_throttle->adjust(allowed_schedulers, uop->decode.partition);
    schedulers.reserve(allowed_schedulers.size());
    for (auto sch : allowed_schedulers)
        schedulers.emplace_back(sch, uop->decode.partition, true);

    assert(!schedulers.empty());

    // FSU uops assign pipes using pre-swizzle lanes; IXU uops use post-swizzle lanes
    if (uop->is_pipe_fsu())
        return least_loaded_sch(schedulers, uop->decode.pre_swizzle_partition, uop->decode.pre_swizzle_lane);
    return least_loaded_sch(schedulers, uop->decode.partition, uop->decode.lane);
}

void IDR::assign_pipe(const Uop& uop)
{
    if (auto sched = pipe_balance(uop); sched)
        assign_sch(uop, *sched);
}

Scheduler IDR::least_loaded_sch(const std::vector<Scheduler>& eligible_sch, unsigned partition, int lane)
{
    std::vector<size_t> counts(sch.size());
    for (uint64_t s = 0; s < eligible_sch.size(); s++) {
        auto sched = eligible_sch[s].scheduler();
        counts[s] = sch[sched].get_count();

        // Take into account uops which have already been assigned a scheduler in this same cycle,
        // and also avoid sending more uops than allowed to a single scheduler. Without smart_least_loaded,
        // if pipe balancing is disabled then all uops will go to the scheduler which had the most
        // credits at the beginning of the cycle.
        if (smart_least_loaded) {
            counts[s] -= sch_assigned_this_cycle[sched];

            // This will try to avoid sending more than 2 uops to a single scheduler. But if we can't avoid it,
            // e.g. if we get four X-only uops at once, the code below will still pick a valid scheduler.
            if (sch_assigned_this_cycle[sched] >= scheduler_write_ports)
                counts[s] = 0;
        }
    }

    bool all_credits_equal{true};
    for (uint64_t s = 1; s < eligible_sch.size(); s++) {
        if (counts[s] != counts[0]) {
            all_credits_equal = false;
            break;
        }
    }

    // Special case for a uop which can go to two schedulers, and they have an equal number of
    // credits: the pipe depends on which partition/lane this uop is in, in ABBA/XYYX pattern.
    // Later this was simplified to just go to a default pipe
    if (eligible_sch.size() > 1 && all_credits_equal) {
        if (simple_default_pipes) {
            // For the specific cases that exist on Connor, XY uops go to X and AB uops go to B.
            // For any other situation (more pipes added, memory uops, etc.) just return a valid option.
            if (eligible_sch.size() == 2 && *eligible_sch[0].sched_type == Sch::X
                && *eligible_sch[1].sched_type == Sch::Y)
                return {Sch::X, partition, true};
            else if (eligible_sch.size() == 2 && *eligible_sch[0].sched_type == Sch::A
                     && *eligible_sch[1].sched_type == Sch::B)
                return {Sch::B, partition, true};
            else
                return eligible_sch[0];
        }

        if (partition == 0 && lane == 0)
            return eligible_sch[0];
        else if (partition == 0 && lane == 1)
            return eligible_sch[1];
        else if (partition == 0 && lane == 2)
            return eligible_sch[0];
        else if (partition == 1 && lane == 2)
            return eligible_sch[1];
        else if (partition == 1 && lane == 1)
            return eligible_sch[0];
        else
            return eligible_sch[1];
    }

    // If the credits aren't tied, then we pick the least loaded scheduler
    auto first = eligible_sch[0];
    Scheduler least_loaded{first};
    auto max_credits = counts[0];
    for (unsigned i = 1; i < eligible_sch.size(); ++i) {
        auto credits = counts[i];
        if (credits > max_credits) {
            max_credits = credits;
            least_loaded = eligible_sch[i];
        }
    }

    return least_loaded;
}

void IDR::reclaim_credits()
{
    while (rob_uop_return->can_take()) { // committed uops from GPC
        auto uop = rob_uop_return->take();
        check_for_resources = true;

        // When a uop commits, the PRF entry previously holding its dest is no longer needed and can
        // be released, and the PRF entry written by this uop takes its place.
        if (uop->is_gather()) {
            AMPERE_ASSERT(!archx::invalid_reg(uop->reg_dst), "Committed uop has PRF entry but invalid dest");
            GatherProxyCreditPtr gprox = uop->get_gather_proxy();
            AMPERE_ASSERT(gprox, "Gather needs a GatherProyCreditPtr");
            DBG_GATHERC(
                "IDR commit, reclaim_credits parent txid: {}, alloc_id: {}", gprox->get_txid(), gprox->get_alloc_id());

            if (gprox->commit_try_finish()) {
                // Why might the prfid not be valid? If the gather ARM
                // instr encountered some sort of partial flush during
                // decoding/execution, we we'll re-execute the ARM
                // gather. In that flush case we release the prfid on
                // the flush (where ever the uops happen to be) and
                // just ignore the writeback to the commited_rf_table.
                Credit prfid = std::move(gprox->prfid);
                if (prfid) {
                    release_credit(committed_rf_table[uop->reg_dst]); // get rid of old writer's credit
                    DBG_GATHERC("idr commit, gather done.  releasing gather credit. txid: {} alloc_id: {}",
                                gprox->get_txid(),
                                gprox->get_alloc_id());
                    committed_rf_table[uop->reg_dst] = std::move(prfid);
                } else {
                    DBG_GATHERC("idr ignoring commit for incomplete gather.  txid: {}, alloc_id: {}",
                                gprox->get_txid(),
                                gprox->get_alloc_id());
                }
            } else {
                DBG_GATHERC("idr defers releasing a gather credit parent (not done yet) txid: {} prfid: {}",
                            gprox->get_txid(),
                            (gprox->prfid_still_valid() ? "valid" : "invalid"));
            }
        } else if (uop->prfid) {
            AMPERE_ASSERT(!archx::invalid_reg(uop->reg_dst), "Committed uop has PRF entry but invalid dest");
            DBGCRT("IDR release credit for reg: {}, preg: {}", uop->reg_dst, uop->prfid->get_id());
            release_credit(committed_rf_table[uop->reg_dst]);
            committed_rf_table[uop->reg_dst] = std::move(uop->prfid);
        }

        if (uop->prfid2) {
            AMPERE_ASSERT(!archx::invalid_reg(uop->reg_dst2), "Committed uop has PRF entry but invalid dest2");
            DBGCRT("IDR release credit2 for reg: {}, preg: {}", uop->reg_dst2, uop->prfid2->get_id());
            release_credit(committed_rf_table[uop->reg_dst2]);
            committed_rf_table[uop->reg_dst2] = std::move(uop->prfid2);
        }

        if (uop->flagsid) {
            archx::LReg flg = archx::LReg::FLAGS;
            if (uop->is_temp_flags_producer()) {
                flg = archx::LReg::FLGT0;
            }
            DBGCRT("IDR release creditf for reg: {}, preg: {}", flg, uop->flagsid->get_id());
            release_credit(committed_rf_table[flg]);
            committed_rf_table[flg] = std::move(uop->flagsid);
        }

        if (uop->robid)
            release_credit(uop->robid);

        AMPERE_ASSERT(dispatched_uops.front() == uop, "Committing uop does not match front of IDR dispatched uops");
        dispatched_uops.pop_front();
    }

    for (auto* credit_return : credit_returns) {
        while ((*credit_return)->can_take()) {
            check_for_resources = true;
            if (auto credit = (*credit_return)->take(); credit)
                credit->release();
        }
    }

    // The IDR-FSU interface will stall for one cycle if more than 2 FSU uops were dispatched to either
    // the X or Y pipe
    if (stall_idr_fsu) {
        // The stall this triggers is temporary, we need to check_for_resources next cycle to see if it has cleared.
        // Otherwise the stall continues until some other credit return sets check_for_resources
        check_for_resources = true;
        if (fsu_schedulers_free)
            stall_idr_fsu = false;
        else
            fsu_schedulers_free = true;
    }
}

std::string IDR::lanes_str(const UopGroup& uops) const
{
    std::string decode_lane_str(uop_decode_width, 'X');

    // Generate Memory uop key for lookup table
    for (unsigned i = 0, lane = 0; i < uops.uops.size(); i++) {
        const auto& uop = uops.uops[i];
        // mem is always first half
        if (uop->cleaved_second_half)
            continue;

        if (uop->is_load() || uop->is_ixu_store()) {
            decode_lane_str[uop_decode_width - uops.lanes[lane] - 1] = 'M';
        } else {
            decode_lane_str[uop_decode_width - uops.lanes[lane] - 1] = 'D';
        }
        lane++;
    }
    return decode_lane_str;
}

void IDR::pmu_backend_events(Txn txn, const ResourceVector& resource)
{
    if (pmu_oldest_load_mem_miss->can_read() && pmu_oldest_load_mem_miss->read())
        ev_pmu(txn, ev_num(archx::PMU::STALL_BACKEND_MEM));
    else if (pmu_oldest_load_cache_miss->can_read() && pmu_oldest_load_cache_miss->read())
        ev_pmu(txn, ev_num(archx::PMU::STALL_BACKEND_L1D));
    else if (pmu_oldest_load_tlb_miss->can_read() && pmu_oldest_load_tlb_miss->read())
        ev_pmu(txn, ev_num(archx::PMU::STALL_BACKEND_TLB));

    if (pmu_oldest_load_ofb_full->can_read() && pmu_oldest_load_ofb_full->read())
        ev_pmu(txn, ev_num(archx::PMU::STALL_BACKEND_OFB_FULL));

    if (pmu_oldest_ldst_valid->can_read() && pmu_oldest_ldst_valid->read())
        ev_pmu(txn, ev_num(archx::PMU::STALL_BACKEND_MEMBOUND));

    if (pmu_oldest_load_st_wait->can_read() && pmu_oldest_load_st_wait->read())
        ev_pmu(txn, ev_num(archx::PMU::STALL_BACKEND_LD_WAIT_ON_ST));

    if (pmu_oldest_ldst_atomic->can_read() && pmu_oldest_ldst_atomic->read())
        ev_pmu(txn, ev_num(archx::PMU::STALL_BACKEND_ATOMIC));

    std::vector<bool> stall_backend_busy(sch.size(), false);
    std::vector<bool> stall_backend_ilock(sch.size(), false);
    std::vector<bool> stall_backend_rename(NUM_RF, false);

    for (unsigned i = 0; i < RESOURCE_COUNT; i++) {
        if (!resource[i]) {
            // FIXME: do we need to add RESOURCE_GATHER_TBL to the PMU stalls?
            if (i < sch.size()) {
                if (!pmu_stall_busy.empty() && pmu_stall_busy[i])
                    stall_backend_busy[i] = true;
                if (!pmu_stall_ilock.empty() && pmu_stall_ilock[i])
                    stall_backend_ilock[i] = true;
            } else if (i < (sch.size() + util::enum_size<BufferResource>())) {
                auto r = static_cast<BufferResource>(i - sch.size());
                switch (r) {
                case BufferResource::ROB:
                    ev_pmu(txn, ev_num(archx::PMU::IDR_STALL_ROB_ID));
                    break;
                case BufferResource::LOB:
                    ev_pmu(txn, ev_num(archx::PMU::IDR_STALL_LOB_ID));
                    break;
                case BufferResource::SOB:
                    ev_pmu(txn, ev_num(archx::PMU::IDR_STALL_SOB_ID));
                    break;
                case BufferResource::BOB:
                    ev_pmu(txn, ev_num(archx::PMU::IDR_STALL_BOB_ID));
                    break;
                case BufferResource::INT_RF:
                case BufferResource::FP_RF:
                case BufferResource::FLAG_RF:
                case BufferResource::PRED_RF:
                    stall_backend_rename[std::to_underlying(r) - std::to_underlying(BufferResource::INT_RF)] = true;
                    break;
                default:
                    break;
                }
            }
        }
    }

    bool any_sched_busy = false;
    for (auto [i, stalled] : std::views::enumerate(stall_backend_busy)) {
        if (stalled) {
            ev_pmu_backend_busy(txn, ev_sch_num(i));
            any_sched_busy = true;
        }
    }
    if (any_sched_busy)
        ev_pmu(txn, ev_num(archx::PMU::STALL_BACKEND_BUSY));

    bool any_sched_ilock = false;
    for (auto [i, stalled] : std::views::enumerate(stall_backend_ilock)) {
        if (stalled) {
            ev_pmu_backend_ilock(txn, ev_sch_num(i));
            any_sched_ilock = true;
        }
    }
    if (any_sched_ilock)
        ev_pmu(txn, ev_num(archx::PMU::STALL_BACKEND_ILOCK));

    bool any_rf_stall = false;
    for (auto [i, stalled] : std::views::enumerate(stall_backend_rename)) {
        if (stalled) {
            ev_pmu(txn, ev_num(stall_backend_rename_pmus[i]));
            any_rf_stall = true;
        }
    }
    if (any_rf_stall)
        ev_pmu(txn, ev_num(archx::PMU::STALL_BACKEND_RENAME));
}

/*************************************************************************
 * Method which collects statistics on a per-cycle basis. This is useful
 * for measuring occupancy for power studies.
 *************************************************************************/
void IDR::stats_collection()
{
    for (unsigned i = 0; i < sch.size(); i++) {
        resource_total[i] += sch[i].get_occupancy();
        resource_maxed[i] += !sch[i].can_acquire();
    }

    for (auto [i, pool] : std::ranges::views::enumerate(buffer_resource_credit_pools)) {
        auto id = sch.size() + i;
        resource_total[id] += pool->get_occupancy();
        resource_maxed[id] += !pool->can_acquire();
    }
}

void IDR::dump(std::ostream& os) const
{
    if (profile_wrong_path) {
        dump_wrong_path_info(os);
    }
    const char nl = '\n';
    os << "Credit pools:\n";
    for (auto [i, pool] : std::ranges::views::enumerate(buffer_resource_credit_pools)) {
        os << '\t' << static_cast<BufferResource>(i) << "  : " << pool->get_count() << nl;
    }

    for (unsigned i = 0; i < sch.size(); ++i) {
        auto s = Scheduler::from_sched(i);
        os << "\tsch " << s.name() << "  : " << sch[i].get_count() << nl;
    }

    os << std::endl;
    os << "Logical to Physical Register Mapping:\n";
    table_print(os);

    if (cleaving_stats_collection) {
        for (const auto& [key, value] : cleaving_stats)
            fmt::print(os, "CLEAVING_STATS: {} ==== {}\n", key.data(), value);
    }

    counter_balancing.dump(os);

    if (decode_stats_collection) {
        for (const auto& [key, value] : decode_stats)
            fmt::print(os,
                       "DECODE_STATS: {} ==== {:08x} {} {}\n",
                       key.data(),
                       std::get<0>(value),
                       std::get<1>(value),
                       std::get<2>(value));

        for (const auto& [key, value] : uopcode_stats)
            fmt::print(os, "UOPCODE_STATS: {} : {}\n", key, value);
    }

    if (!detailed_stats_collection)
        return;

    Cycle total_time = get_cycle();
    os << "\nDetailed stats for design and power, in .csv form\n";

    os << "metric, cycles, ";
    for (unsigned i = 0; i < RESOURCE_COUNT; i++) {
        os << get_resource_name(i) << " avg, ";
    }
    for (unsigned i = 0; i < RESOURCE_COUNT; i++) {
        if (i > 0)
            os << ", ";
        os << get_resource_name(i) << " full";
    }
    os << nl;

    os << "IDR_STATS, " << total_time << ", ";

    for (unsigned i = 0; i < RESOURCE_COUNT; i++) {
        os << resource_total[i] / (float)total_time << ", ";
    }
    for (unsigned i = 0; i < RESOURCE_COUNT; i++) {
        os << resource_maxed[i] << ", ";
    }

    os << std::endl;
}

void IDR::table_print(std::ostream& os) const
{
    fmt::print("LReg\tCommit PRF\tSpec PRF\n");

    for (int r = archx::LReg::L0; r <= archx::LReg::L30; r++)
        print_rf_entry(os, r, "X");

    print_rf_entry(os, archx::LReg::SP, "SP");
    print_rf_entry(os, archx::LReg::s0, "s0");
    print_rf_entry(os, archx::LReg::s1, "s1");

    os << "\n";

    for (int r = archx::LReg::V0; r <= archx::LReg::V31; r++)
        print_rf_entry(os, r - archx::LReg::V0, "V");

    for (int r = archx::LReg::t0; r <= archx::LReg::t4; r++)
        print_rf_entry(os, r - archx::LReg::t0, "t");

    os << "\n";

    print_rf_entry(os, archx::LReg::FLAGS, "FLAGS");
    print_rf_entry(os, archx::LReg::FLGT0, "FLGT0");

    os << "\n";
}

void IDR::print_rf_entry(std::ostream& os, int r, const char* prefix) const
{
    auto committed_id = committed_rf_table[r] ? committed_rf_table[r]->get_id() : 0;
    auto spec_id = spec_rf_table[r];
    fmt::print(os, "{:<6}{:10}{:10}\n", fmt::format("{}{}", prefix, r), committed_id, spec_id);
}

void IDR::set_memop(const std::shared_ptr<ampere::archx::Instruction>& archx_instruction, Uop& uop, unsigned memop_idx)
{
    uop->virt_addr = archx_instruction->virtual_address(memop_idx);
    unsigned bytes = archx_instruction->memop_bytes(memop_idx);
    if (bytes) {
        uop->set_dynamic_mem_datasize(bytes);
    }
}

void IDR::init_align_limits()
{
    branch_count = 0;
    SIR_count = 0;
}

bool IDR::is_new_SIR(ProgramCounter pc, ProgramCounter prev_pc)
{
    // Any time we cross a cache line boundary, it's a new SIR
    if (pc.line_number() != prev_pc.line_number())
        return true;

    // This instruction is the same as the last one, so not a new SIR
    if (pc == prev_pc)
        return false;

    // This instruction is sequential to the last one, so not a new SIR
    if (pc == prev_pc + 4)
        return false;

    // This instruction is not sequential to the last one, so it's a new SIR
    return true;
}

bool IDR::align_limits_test(bool is_branch, const std::vector<Instruction>& instrs, bool emit)
{
    if (branch_count >= align_branch_limit && is_branch) {
        if (emit)
            emit_align_reason(IDRAlignReason::TooManyBranch);
        return false; // cannot add the branch
    }

    auto test_SIR_count = SIR_count;
    auto test_prev_pc = previous_pc;
    for (const auto& inst : instrs) {
        if (is_new_SIR(inst->pc(), test_prev_pc))
            test_SIR_count++;
        test_prev_pc = inst->pc();
    }
    if (test_SIR_count > align_SIR_limit) {
        if (emit)
            emit_align_reason(IDRAlignReason::TooManySIR);
        return false; // cannot add new sequential instruction run
    }

    return true;
}

void IDR::align_limits_update(const Uop& uop)
{
    if (uop->is_branch())
        branch_count++;
    AMPERE_ASSERT(branch_count <= align_branch_limit, "Too many branches in uop group");

    for (const auto& inst : uop->instructions) {
        if (is_new_SIR(inst->pc(), previous_pc))
            SIR_count++;
        previous_pc = inst->pc();
        AMPERE_ASSERT(SIR_count <= align_SIR_limit, "Too many SIRs in uop group");
    }
}

void IDR::emit_align_reason(IDRAlignReason r)
{
    ev_align_limited(r);
    if (r == IDRAlignReason::IQRanOut)
        ev_pmu(ev_num(archx::PMU::STALL_FRONTEND_IQ_BW));
    else if (r == IDRAlignReason::SplitTriple || r == IDRAlignReason::SplitQuad || r == IDRAlignReason::TooManySIR
             || r == IDRAlignReason::TooManyBranch || r == IDRAlignReason::TooManyFusionDouble
             || r == IDRAlignReason::TooManyFusionTriple || r == IDRAlignReason::TooManyFusionQuad
             || r == IDRAlignReason::CannotFitDouble || r == IDRAlignReason::CannotFitTriple
             || r == IDRAlignReason::CannotFitQuad || r == IDRAlignReason::FusionDelayHint
             || r == IDRAlignReason::DecoderFusionDoesNotFit || r == IDRAlignReason::AutoFusionDoesNotFit)
        ev_pmu(ev_num(archx::PMU::STALL_FRONTEND_DECODER));
}

uint64_t IDR::cleaving_scheduler_vector(const Uop& uop) const
{
    uint64_t sched_vec{0};
    auto allowed_schedulers = Scheduler::eligible_scheduler_types(uop->allowed_pipes());
    scheduler_throttle->adjust(allowed_schedulers, uop->decode.partition);
    for (auto sch : allowed_schedulers)
        sched_vec |= (1ul << sch);

    return sched_vec;
}

bool IDR::is_aarch64_fp_or_sve(const Uop& uop)
{
    // RTL identifies FSU instructions (which are not allowed to cleave) by looking at specific opcode bits.
    // This means that vector loads can't cleave, even though they aren't actually FSU uops.
    Bitfield opcode{uop->instructions.front()->opcode()};
    if (opcode.extract_range(27, 26) == 0b11) // FP opcodes
        return true;
    if (opcode.extract_range(28, 25) == 0b0010) // SVE opcodes
        return true;

    return false;
}

bool IDR::can_cleave(const Uop& ua, const Uop& ub) const
{
    // If the previous uop is already cleaved, we can't cleave to it
    AMPERE_ASSERT(!ua->cleaved_first_half, "Tried to cleave another uop to the first half of a pair");
    if (ua->cleaved_second_half)
        return false;

    // The default wrong-path uop is an add-immediate, which causes lots of fake cleaving and messes up the stats
    if (ua->ainst()->artificial_encoding())
        return false;

    // Panthera-specific check to avoid cleaving across interrupts, etc.
    if (ua->requires_flush() || ub->requires_flush())
        return false;

    // The first uop cannot be mispredictable, because then we would flush with half of a cleaved uop complete
    if (ua->could_mispredict() && !cleaving_allow_branch_first)
        return false;

    // The second uop cannot fault, because that would also flush with a half-complete cleaved uop
    if (ub->could_fault() && !cleaving_allow_fault_second)
        return false;

    // No way are we going to cleave two fused instructions together, that's crazy talk
    if (ua->is_fused() && ub->is_fused())
        return false;

    // Cleaving barriers and other things that have at-commit behavior causes bad things to happen
    if ((ua->is_ordered() && ub->is_lsu()) || (ua->is_lsu() && ub->is_ordered()))
        return false;

    // Internal cleaving is specified in the XLAT and handled separately.
    // We're not even considering cleaving across instructions when one instruction is multi-uop.
    if (!ua->is_single_uop_instr() || !ub->is_single_uop_instr())
        return false;

    // XLAT can block or restrict cleaving for specific instructions
    if (ua->archx_uop->no_cleave || ub->archx_uop->no_cleave)
        return false;
    if (ub->archx_uop->cleavlet0_only)
        return false;

    // We can only rename one vector dest per uop
    if (is_vector(ua->reg_dst) && is_vector(ub->reg_dst) && !cleaving_allow_two_vecdst)
        return false;

    // Isolated uops can't cleave
    if (ua->is_isolated() || ub->is_isolated())
        return false;

    // The first uop can't use a PC-sized immediate, but the second uop can
    if (ua->archx_uop->use_pc || ua->archx_uop->set_imm_to_pc_next)
        return false;

    // Only one of the uops can write flags
    bool ua_writes_flags = ua->is_flag_producer() || ua->is_temp_flags_producer();
    bool ub_writes_flags = ub->is_flag_producer() || ub->is_temp_flags_producer();
    if (ua_writes_flags && ub_writes_flags && !cleaving_allow_two_flgdst)
        return false;

    // The uops must not need to go to the same scheduler; this allows us to avoid ever needing to write three
    // uops into the same scheduler at once. Get a list of the schedulers each half can go to; if the uops
    // have the same scheduler list and there is only one scheduler in it, then they can't cleave.
    auto ua_scheds = cleaving_scheduler_vector(ua);
    auto ub_scheds = cleaving_scheduler_vector(ub);
    if (ua_scheds == ub_scheds && __builtin_popcount(ua_scheds) == 1 && !cleaving_allow_same_sched)
        return false;

    // We might allow cleaving of two FSU uops, or only one FSU with something non-FSU, or only STDV.
    int fsu_count{0};
    if (is_aarch64_fp_or_sve(ua))
        fsu_count++;
    if (is_aarch64_fp_or_sve(ub))
        fsu_count++;
    if (fsu_count > cleaving_fsu_limit)
        return false;

    // We might cleave a fused instruction with a normal single-uop instruction
    if ((ua->is_fused() || ub->is_fused()) && !cleaving_allow_fused)
        return false;

    // We don't want to deal with cleaving a uop which has two dests
    if (ua->is_producer() && ua->has_dest2())
        return false;
    if (ub->is_producer() && ub->has_dest2())
        return false;

    // We might do a special case to allow cleaving of the two uops of a super-SUS store with post-increment (store +
    // add)
    bool super_sus_free_pass = cleaving_allow_super_sus && ua->is_super_sus() && ua->same_instruction(ub);
    if (!super_sus_free_pass) {
        // The two uops can only have 3 valid src1/2/3 registers between them. In this case 'valid' means that the
        // source is specified in the XLAT, even if the logical register turns out to be XZR -- so explictly check for
        // LReg::INVALID, instead of using archx::invalid_reg(), which also considers XZR invalid.
        int src_count{0};
        for (int s = 0; s < 3; s++) {
            if (ua->reg_src[s] != LReg::INVALID)
                src_count++;
            if (ub->reg_src[s] != LReg::INVALID)
                src_count++;
        }
        if (src_count > cleaving_max_srcs)
            return false;

        if (!cleaving_no_src_restrictions) {
            // src3 can only be flags (not GPR or vector)
            if (ua->reg_src[2] != LReg::INVALID && !is_flag(ua->reg_src[2]))
                return false;
            if (ub->reg_src[2] != LReg::INVALID && !is_flag(ub->reg_src[2]))
                return false;

            // And only one flag source allowed total
            if (!invalid_reg(ua->reg_src[2]) && !invalid_reg(ub->reg_src[2]))
                return false;

            // Additional restrictions to reduce the complexity of shuffling sources around:
            // 1) Can't have two src1 valids and a flag source
            // 2) Can't have two src2 valids and a flag source
            bool flag_src = is_flag(ua->reg_src[2]) || is_flag(ub->reg_src[2]);
            if (ua->reg_src[0] != LReg::INVALID && ub->reg_src[0] != LReg::INVALID && flag_src)
                return false;
            if (ua->reg_src[1] != LReg::INVALID && ub->reg_src[1] != LReg::INVALID && flag_src)
                return false;
        }
    }

    // Moves are complicated because of move elimination, so they might be supported as only one or the other half
    // of the pair, or not at all.
    if (ua->is_int_move_reg() && !cleaving_allow_move_first)
        return false;
    if (ub->is_int_move_reg() && !cleaving_allow_move_second)
        return false;

    // We made it! These uops can cleave.
    return true;
}

void IDR::collect_cleaving_stats(const Uop& uop1, const Uop& uop2)
{
    if (!cleaving_stats_collection)
        return;

    const auto& ua = uop1->archx_uop;
    const auto& ub = uop2->archx_uop;
    std::ostringstream source;
    source << fmt::format("{} {} : {} : {} +++ {}",
                          ua->opcode,
                          ub->opcode,
                          uop1->same_instruction(uop2) ? "SAME" : "DIFF",
                          uop1->instructions[0]->archx_instruction->decode_source(),
                          uop2->instructions[0]->archx_instruction->decode_source());
    cleaving_stats[source.str()]++;
}

bool IDR::fusion_delay(unsigned instr_remaining)
{
    if (!fusion_delay_enable)
        return false;

    // We only consider delaying decode if the instruction is in the last slot and the IQ is non-empty
    if (instr_remaining == 1 && !instruction_fifo.empty()) {
        auto inst = instruction_fifo.front();

        // Ripley AES needs a special case because it's not in the XLAT
        if (fusion_aes_delay_ripley && (inst->archx_instruction->is_aesd() || inst->archx_instruction->is_aese()))
            return true;

        // Otherwise the XLAT tells us that this is an instruction which should be delayed
        return inst->archx_instruction->is_fusion_delay_hint();
    }

    return false;
}

bool IDR::can_start_2uop_decode(unsigned decode_lane, unsigned fusions)
{
    // Trinity & prior hardware do not decode a >=2uop instr if it follows a pair of fusions (4 instr
    // that generate 2 uops) even though there is room in the output group for 2 more uops.
    if (fusions >= align_fusion_limit_2uop) {
        emit_align_reason(IDRAlignReason::TooManyFusionDouble);
        return false;
    }

    switch (decode_lane) {
    case 0:
    case 1:
    case 2:
        // We can always start a 2-uop instruction in lanes 0/1/2
        return true;
    case 3:
        if (split_double_uop) {
            // Ripley and beyond can start in lane 3 (split 2-uop instr across cycles)
            return true;
        } else {
            // Furiosa has to decode the whole thing in one cycle
            emit_align_reason(IDRAlignReason::CannotFitDouble);
            return false;
        }
    default:
        // If someone has set decode to be more than 4 wide, they should probably add explicit new cases here.
        // But by default assume that 2-uop instructions can start in any lane.
        return true;
    }
}

bool IDR::can_start_3uop_decode(unsigned decode_lane, unsigned fusions)
{
    // Connor & prior hardware do not decode a >=3uop instr if it follows a pair of fusions (4 instr
    // that generate 2 uops) even though there is room in the output group for 2 more uops.
    if (fusions >= align_fusion_limit_long) {
        emit_align_reason(IDRAlignReason::TooManyFusionTriple);
        return false;
    }

    switch (decode_lane) {
    case 0:
    case 1:
    case 2:
        // We can always start a 3-uop instruction in lanes 0/1/2
        return true;
    case 3:
    default:
        // On all current cores we cannot start a 3-uop instruction in lane 3, but there's a param for that.
        //
        // If someone has set decode to be more than 4 wide, they should probably add explicit new cases here.
        // But by default, assume that we can start anywhere but the last lane.
        if (decode_lane < current_decode_width - 1 || allow_split_one_triple)
            return true;

        emit_align_reason(IDRAlignReason::CannotFitTriple);
        return false;
    }

    return true;
}

bool IDR::can_start_quad_decode(unsigned& decode_lane, unsigned fusions)
{
    // Connor & prior hardware do not decode a >=3uop instr if it follows a pair of fusions (4 instr
    // that generate 2 uops) even though there is room in the output group for 2 more uops.
    if (fusions >= align_fusion_limit_long) {
        emit_align_reason(IDRAlignReason::TooManyFusionQuad);
        return false;
    }

    // 4+ uop instructions can start in lanes 0 and 2.
    switch (decode_lane) {
    case 0:
        return true;
    case 1:
        // 6-wide mode allows a quad to start anywhere but the last lane.
        if (!six_wide_dispatch) {
            decode_lane = 2;
            emit_align_reason(IDRAlignReason::SplitQuad);
        }
        return true;
    case 2:
        return true;
    case 3:
        emit_align_reason(IDRAlignReason::CannotFitQuad);
        return false;
    default:
        // If someone has set decode to be more than 4 wide, they should probably add explicit new cases here.
        // But by default, assume that we can start anywhere but the last lane.
        if (decode_lane < (current_decode_width - 1)) {
            return true;
        } else {
            emit_align_reason(IDRAlignReason::CannotFitQuad);
            return false;
        }
    }
}

bool IDR::get_more_uops(const Uop& prev_uop, unsigned instr_remaining, unsigned& decode_lane, unsigned& fusions)
{
    // Handle fusion delay hints: If instruction is in the last
    // slot and can be fused, let's delay this instruction until
    // the next cycle hoping we can fuse it.
    if (fusion_delay(instr_remaining)) {
        emit_align_reason(IDRAlignReason::FusionDelayHint);
        return false;
    }

    pending_uops = decode_instructions(instr_remaining); // DECODE ARM INSTRUCTIONS
    size_t uop_count = pending_uops.size();
    if (uop_count == 0) {
        emit_align_reason(IDRAlignReason::IQRanOut);
        return false;
    }
    const unsigned instr_count = pending_uops[0]->instructions.size();

    // If this is a multi-uop instruction, check whether any of the uops can cleave. If so, we mark the second one
    // so that the main align() function knows that it should share a lane with the previous uop. We also decrement
    // uop_count, because this pair of uops only counts for one in the can_start_*_decode() functions below.
    if (cleaving_enable_internal && uop_count > 1) {
        for (const auto& pending_uop : pending_uops) {
            if (pending_uop->archx_uop->cleavlet1) {
                pending_uop->cleaved_second_half = true;
                pending_uop->instructions.front()->has_internal_cleaving = true;
                uop_count--;
            }
        }
    }

    // Decide whether we can start decoding this instruction now, and if so what lane it should start decode in
    if (pending_uops[0]->is_isolated()) {
        // We can only decode isolated instructions starting in lane 0
        bool decode_isolated = (decode_lane == 0);
        if (!decode_isolated)
            emit_align_reason(IDRAlignReason::IsolatedStart);
        return decode_isolated;
    } else {
        // 1-uop instructions can go anywhere. For longer instructions, check various restrictions.
        // Note that can_start_quad_decode() may modify decode_lane to where the instruction should
        // start (if it can be decoded at all).
        switch (uop_count) {
        case 1:
            if (instr_count > 1)
                fusions++;

            // Check whether this uop can cleave with the previous one.
            if (cleaving_enable_external && prev_uop && can_cleave(prev_uop, pending_uops[0])) {
                DBGFUS("Cleaving uops for inum {} and inum {}",
                       prev_uop->ainst()->inum().id(),
                       pending_uops[0]->ainst()->inum().id());
                pending_uops[0]->cleaved_second_half = true;
            }

            return true;
        case 2:
            return can_start_2uop_decode(decode_lane, fusions);
        case 3:
            return can_start_3uop_decode(decode_lane, fusions);
        default: // >= 4  called "quads"
            return can_start_quad_decode(decode_lane, fusions);
        }
    }
}

bool IDR::align(Pipeline<UopGroup>::Data uop_group)
{
    // Panthera perf: quick early-out check if we have nothing to do
    if (pending_uops.empty() && instruction_fifo.empty()) {
        emit_align_reason(IDRAlignReason::IQEmpty);
        return true;
    }

    unsigned instr_remaining = inst_read_width;
    unsigned decode_lane = 0;
    unsigned fusions = 0;
    init_align_limits();

    // Cleaving overview: get_more_uops() checks for cleaving. If it decodes a single-uop instruction, it checks whether
    // it can cleave with the previous uop in the group (if there is one). If it decodes a multi-uop instruction, it
    // looks at each pair of uops in the flow to see whether they can cleave. In either case, if it finds a cleavable
    // pair it marks only the second uop, setting cleaved_second_half.
    //
    // As we add uops to the group, if we see that a uop is the second half of a pair we assign it the same lane as the
    // previous uop. And if it is accepted into the group, we mark the prior uop as being the first half of the pair.

    while (instr_remaining) {
        bool decoded_something{false};

        // If we have no uops waiting to be put into groups, go get some more by decoding an instruction
        if (pending_uops.empty()) {
            // Note that get_more_uops() may modify decode_lane and fusions
            auto prev_uop = !uop_group.initialized() ? nullptr : uop_group.get().uops.back();
            decoded_something = get_more_uops(prev_uop, instr_remaining, decode_lane, fusions);
            if (!decoded_something)
                break;
        }

        // Now try to add a uop to the group
        const auto& uop = pending_uops.front();

        // The second half of a cleaved pair occupies the same decode lane as the first half
        if (uop->cleaved_second_half)
            decode_lane--;

        // Check whether we can't add a uop to the group because we are out of lanes or have to many dests/branches/SIRs
        if (decode_lane == current_decode_width
            || !align_limits_test(uop->is_branch(), uop->instructions, true /*emit align reason*/))
        {
            // If we just decoded an instruction but can't use it, throw it away because we might get a different answer
            // when we decode it next cycle (e.g. maybe it didn't fuse/cleave this time, but will next time).
            if (decoded_something)
                pending_uops.clear();
            break;
        }

        // Now we know we can accept this uop, add it to the group.
        if (!uop_group.initialized())
            uop_group = UopGroup{current_decode_width};
        if (uop->cleaved_second_half) {
            AMPERE_ASSERT(uop_group.get().uops.size() > 0, "First uop in group is 2nd half of cleaved pair.");
            auto& prev_uop = uop_group.get().uops.back();
            prev_uop->cleaved_first_half = true;
            collect_cleaving_stats(prev_uop, uop);
        } else {
            // Keep track of how many lanes contain uops
            uop_group.get().lanes_used++;
        }
        align_limits_update(uop);
        uop_group.get().uops.push_back(uop);
        pending_uops.pop_front();
        decode_lane++;

        // Remove any instructions which were completed by this uop from the IQ
        for (int i = 0; i < uop->archx_uop->end_inst; i++) {
            const auto& inst = instruction_fifo.front();
            if (inst->wrong_path()) {
                const auto emulated = inst->emulated();
                if (adis::load(inst->adis_info())) {
                    ev_wrong_path(inst, ev_emulated(emulated), ev_emulated_load(emulated));
                } else if (adis::store(inst->adis_info())) {
                    ev_wrong_path(inst, ev_emulated(emulated), ev_emulated_store(emulated));
                } else {
                    ev_wrong_path(inst, ev_emulated(emulated));
                }
                if (profile_wrong_path && !inst->emulated()) {
                    tally_wrong_path_instr(inst);
                }
            }
            instruction_fifo.pop_front();
            instr_remaining--;
        }

        // If this was the last uop of an isolated instruction and the group is not full,
        // we can't decode anything else this cycle.
        if (uop->archx_uop->end_inst && uop->is_isolated() && decode_lane < current_decode_width) {
            emit_align_reason(IDRAlignReason::IsolatedEnd);
            break;
        }
    }

    // Make sure cleaved uops always come in pairs
    auto& uops = uop_group.get().uops;
    auto group_size = uop_group.get().uops.size();
    for (uint64_t i = 0; i < group_size; i++) {
        AMPERE_ASSERT(!(uops[i]->cleaved_first_half && uops[i]->cleaved_second_half),
                      "Uop has both cleaving halves set");
        if (uops[i]->cleaved_first_half) {
            AMPERE_ASSERT(i < group_size - 1, "Last uop in group is first half of cleaved pair");
            AMPERE_ASSERT(uops[i + 1]->cleaved_second_half, "Cleaved first half is missing its second half");
        } else if (uops[i]->cleaved_second_half) {
            AMPERE_ASSERT(i > 0, "First uop is second half of cleaved pair");
            AMPERE_ASSERT(uops[i - 1]->cleaved_first_half, "Cleaved second half is missing its first half");
        }
    }

    for (const auto& uop : uop_group.get().uops) {
        // We only emit the fusion type event on the last uop to avoid
        // over-counting fusion for (proposed) fusion of multi-uop
        // sequences.
        if (uop->archx_uop->end_inst && uop->fusion_type != FusionType::INVALID) {
            ev_align(uop, inst_fusion_data(uop->fusion_type), cleaving_data(uop->cleaved_second_half));
        } else {
            ev_align(uop, cleaving_data(uop->cleaved_second_half));
        }
    }

    DBG_ALGN("EMITTED {}", uop_group.get().uops.size());
    return true;
}

void IDR::modify_fsu_unit(const Uop& uop)
{
    // the ev_crypto_bypass event is now decode time (not post-execute as in prior code).
    auto crypto_unit = uop->archx_uop->info->fsu_crypto_unit;
    if (crypto_unit != FsuUnit::NONE) {
        ev_crypto_bypass(uop);
        uop->update_fsu_unit(crypto_unit);
    }
}

bool IDR::decode(Pipeline<UopGroup>::Data uop_group)
{
    AMPERE_ASSERT(!uop_group.get().uops.empty(), "IDR decode stage ran, but no valid uops");

    std::string old_lanes = lanes_str(uop_group.get());
    auto did_ld_balance = load_balancing.balance(uop_group.get());
    std::string middle_lanes = lanes_str(uop_group.get());

    uop_group.get().assign_pre_swizzle_banks();

    // Perform swizzle balancing
    auto swizzle_str = swizzle.swizzle_uops(uop_group.get(), load_balancing, sch);
    counter_balancing.balance(uop_group.get(), sch);
    std::string new_lanes = lanes_str(uop_group.get());

    // Assign banks/partitions from lane assignment
    uop_group.get().assign_banks();

    // We may return IQ credits here in DR03
    if (dr03_iq_credit_return)
        iq_credit_return(uop_group.get());

    for (auto uop : uop_group.get().uops) {
        if (fast_crypto_bypass && recent_crypto_counter != 0) {
            modify_fsu_unit(uop); // make some uops execute on Crypto or Bypass units.
        }
    }

    bool recent_crypto{false};
    for (const auto& uop : uop_group.get().uops) {
        if (uop->is_crypto()) {
            recent_crypto = true;
            break;
        }
    }

    if (recent_crypto)
        recent_crypto_counter = recent_crypto_window;
    else if (recent_crypto_counter)
        recent_crypto_counter--;

    for (const auto& uop : uop_group.get().uops) {
        int cloven{0};
        if (uop->cleaved_first_half)
            cloven = 1;
        else if (uop->cleaved_second_half)
            cloven = 2;

        ev_uop_decode(uop,
                      uopcode_disasm(disassemble_uop(uop)),
                      uop_group_ld_balance(did_ld_balance),
                      uop_group_swizzle_type(swizzle_str),
                      lanes_start_data(old_lanes),
                      lanes_middle_data(middle_lanes),
                      lanes_end_data(new_lanes),
                      ixu_partition_data(uop->decode.partition),
                      cleaving_data(cloven));
    }

    counter_balancing.update_stats(uop_group.get());

    return true;
}

void IDR::cleaving_assign_pipes(const Uop& ua, const Uop& ub)
{
    // If both halves of a cleaved uop go to A/B or X/Y, then we send one half to each scheduler. Pairs which go to
    // different schedulers (e.g. load-add) are handled by the normal pipe balancing.
    if (ua->is_pipe_ab() && ub->is_pipe_ab()) {
        if (ua->is_Aonly()) {
            AMPERE_ASSERT(!ub->is_Aonly(), "both halves need A: {} {}", ua->archx_uop->opcode, ub->archx_uop->opcode);
            assign_sch(ua, Sch::A);
            assign_sch(ub, Sch::B);
        } else if (ua->is_Bonly()) {
            AMPERE_ASSERT(!ub->is_Bonly(), "both halves need B: {} {}", ua->archx_uop->opcode, ub->archx_uop->opcode);
            assign_sch(ua, Sch::B);
            assign_sch(ub, Sch::A);
        } else if (ub->is_Aonly()) {
            AMPERE_ASSERT(!ua->is_Aonly(), "both halves need A: {} {}", ua->archx_uop->opcode, ub->archx_uop->opcode);
            assign_sch(ua, Sch::B);
            assign_sch(ub, Sch::A);
        } else if (ub->is_Bonly()) {
            AMPERE_ASSERT(!ua->is_Bonly(), "both halves need B: {} {}", ua->archx_uop->opcode, ub->archx_uop->opcode);
            assign_sch(ua, Sch::A);
            assign_sch(ub, Sch::B);
        } else {
            assign_sch(ua, Sch::A);
            assign_sch(ub, Sch::B);
        }
    } else if (ua->is_pipe_xyz() && ub->is_pipe_xyz()) {
        // We're looking for uops which can only go to either the X or Y scheduler here, so no need to consider Z-pipe
        // uops -- anything that executes on Z can go to either scheduler.
        if (ua->is_Xonly()) {
            AMPERE_ASSERT(!ub->is_Xonly(), "both halves need X: {} {}", ua->archx_uop->opcode, ub->archx_uop->opcode);
            assign_sch(ua, Sch::X);
            assign_sch(ub, Sch::Y);
        } else if (ua->is_Yonly()) {
            AMPERE_ASSERT(!ub->is_Yonly(), "both halves need Y: {} {}", ua->archx_uop->opcode, ub->archx_uop->opcode);
            assign_sch(ua, Sch::Y);
            assign_sch(ub, Sch::X);
        } else if (ub->is_Xonly()) {
            AMPERE_ASSERT(!ua->is_Xonly(), "both halves need X: {} {}", ua->archx_uop->opcode, ub->archx_uop->opcode);
            assign_sch(ua, Sch::Y);
            assign_sch(ub, Sch::X);
        } else if (ub->is_Yonly()) {
            AMPERE_ASSERT(!ua->is_Yonly(), "both halves need Y: {} {}", ua->archx_uop->opcode, ub->archx_uop->opcode);
            assign_sch(ua, Sch::X);
            assign_sch(ub, Sch::Y);
        } else {
            assign_sch(ua, Sch::X);
            assign_sch(ub, Sch::Y);
        }
    }
}

void IDR::assign_pipes(const UopGroup& uop_group)
{
    // Clear out any previous pipe assignments so that they don't confuse the code below -- we will
    // re-evaluate pipe assignments every cycle while stalled.
    for (const auto& uop : uop_group.uops)
        uop->sch.reset_sched_type();
    for (int& s : sch_assigned_this_cycle)
        s = 0;

    if (!pipe_balancing) {
        for (const auto& uop : uop_group.uops) {
            assign_pipe(uop);
        }
        return;
    }

    for (auto* assign : assignments)
        assign->start_assignment();

    for (unsigned partition = 0; partition < MAX_PARTITIONS; partition++) {
        std::vector<Uop> partition_int_uops, partition_mem_uops, partition_fsu_uops;

        // First go through and find any cleaved pairs where both halves go to A/B or X/Y, and send one half to each
        // pipe
        for (uint64_t i = 0; i < uop_group.uops.size(); i++) {
            if (uop_group.uops[i]->cleaved_first_half)
                cleaving_assign_pipes(uop_group.uops[i], uop_group.uops[i + 1]);
        }

        for (const auto& uop : uop_group.uops) {
            // For the rest of pipe assignment, we consider only uops which were not already assigned by the cleaving
            // check above
            if (!uop->sch.sched_type) {
                if (uop->is_pipe_ab() && uop->decode.partition == partition)
                    // Integer uops use the post-swizzle partition
                    partition_int_uops.push_back(uop);
                else if (uop->is_pipe_cd() && uop->decode.partition == partition)
                    // Memory uops also use the post-swizzle partition
                    partition_mem_uops.push_back(uop);
                else if (uop->is_pipe_fsu() && uop->decode.pre_swizzle_partition == partition)
                    // But FSU uops use the pre-swizzle partition
                    partition_fsu_uops.push_back(uop);
            }
        }

        // AB and XY uops do special balancing between the two schedulers, if there are two uops of the same type
        // in the same partition (otherwise they just send the uop to the least-loaded scheduler).
        // Since each partition only has one memory scheduler, mem uops just use the default assignment.
        assign_schedulers(partition_int_uops, int_scheduler_assignment->assign_uops(partition_int_uops));
        assign_schedulers(partition_fsu_uops, fp_scheduler_assignment->assign_uops(partition_fsu_uops));

        for (auto& uop : partition_mem_uops)
            assign_pipe(uop);
    }

    // Override all other pipe assignment for FP divider uops, and just alternate between pipes
    if (fdiv_ping_pong) {
        for (const auto& uop : uop_group.uops) {
            if (uop->fsu_unit() == archx::FsuUnit::DV
                && ((uop->allowed_pipes() & archx::Uoppipe::X) && (uop->allowed_pipes() & archx::Uoppipe::Y)))
            {
                auto& div_sched = next_div_scheduler[uop->sch.partition];
                assign_sch(uop, div_sched);
                div_sched = (div_sched == Sch::X) ? Sch::Y : Sch::X;
            }
        }
    }
}

bool IDR::rename(Pipeline<UopGroup>::Data uop_group)
{
    AMPERE_ASSERT(!uop_group.get().uops.empty(), "IDR rename stage ran, but no valid uops");

    if (get_cycle() <= stall_recovery_end) {
        ev_recovery_stall(uop_group.get().uops[0]);
        return false;
    }

    // Don't dispatch uops after a no-restart flush -- another flush will be coming soon which will start us up again.
    // FIXME: Really this should be done further up the pipe, but I couldn't get that to work.
    if (idr_no_restart)
        return false;

    // If we saw a barrier instruction and are waiting for the ROB to become
    // empty, then don't let anything proceed until we see the rob_empty signal.
    // We add a little delay from when the barrier was seen to make sure it has
    // shown up in the ROB, otherwise we might complete it immediately if the ROB
    // is already empty.
    if (waiting_for_rob_empty) {
        if (rob_empty && barrier_reaches_rob <= get_cycle()) {
            waiting_for_rob_empty = false;
        } else {
            ev_pmu(ev_num(archx::PMU::IDR_STALL_DRAIN));
            ev_pmu(uop_group.get().uops[0], ev_num(archx::PMU::STALL_BACKEND));
            return false;
        }
    }

    if (waiting_for_sob_empty) {
        if (sob.all_free()) {
            waiting_for_sob_empty = false;
        } else {
            return false;
        }
    }

    if (waiting_for_flush) {
        ev_pmu(ev_num(archx::PMU::IDR_STALL_DRAIN));
        ev_pmu(uop_group.get().uops[0], ev_num(archx::PMU::STALL_BACKEND));
        return false;
    }

    // Within each partition, decide which pipes the uops should be sent to
    assign_pipes(uop_group.get());

    // Count up all the resources needed by this uop group
    count_resources(uop_group.get());

    // Check and make sure we have the resources needed for the uop group before dispatching
    // Panthera perf: check_for_resources is a global flag which is cleared when IDR stalls and set
    // whenever we receive credits for some resource, or flush. This avoids making the same check
    // over and over every cycle when nothing has changed.
    if (check_for_resources)
        resource_vector = check_resources(uop_group.get());
    if (jit_scheduler && !resource_vector.all()) {
        if (schedule_jit(uop_group.get()))
            resource_vector = check_resources(uop_group.get());
    }

    previously_backend_stalled = !resource_vector.all();

    if (!previously_backend_stalled) {
        // If enabled, every 1024 uop groups there is a 50/50 chance that we will stall for one cycle
        // Used to measure sensitivity of traces to tiny fluctuations in execution timing
        if (random_jitter && (++jitter_counter & 0x3ff) == 0)
            if (random.get<bool>())
                return false;

        assign_alloc_ids(uop_group.get());

        // update_swob returns true if a uop requested a drain and flush
        if (update_swob(uop_group.get())) {
            waiting_for_flush = true;
            flush_req_to_gpc->put(uop_group.get().uops[0]);
            return false;
        }

        if (uop_group.get().uops[0]->is_pre_dispatch_drain() && !rob_empty) {
            AMPERE_ASSERT(uop_group.get().uops[0]->is_isolated(), "Pre-dispatch drain uops must be isolated");
            waiting_for_rob_empty = true;
            barrier_reaches_rob = get_cycle();

            // MPAM register writes impact post-commit stores, sp drain them first
            if (uop_group.get().uops[0]->is_mtspr()) {
                switch (uop_group.get().uops[0]->archx_uop->imm) {
                case 0xc529: // MPAM0_EL1
                case 0xc528: // MPAM1_EL1
                case 0xe528: // MPAM2_EL2
                case 0xf528: // MPAM3_EL3
                case 0xe520: // MPAMHCR_EL2
                case 0xc524: // MPAMIDR_EL1
                case 0xe530: // MPAMVPM0_EL2
                case 0xe531: // MPAMVPM0_EL2
                case 0xe532: // MPAMVPM0_EL2
                case 0xe533: // MPAMVPM0_EL2
                case 0xe534: // MPAMVPM0_EL2
                case 0xe535: // MPAMVPM0_EL2
                case 0xe536: // MPAMVPM0_EL2
                case 0xe537: // MPAMVPM0_EL2
                case 0xe521: // MPAMVPMV_EL2
                    waiting_for_sob_empty = true;
                    break;
                default:
                    break;
                }
            }

            return false;
        }

        for (const auto& uop : uop_group.get().uops) {
            assign_resources(uop);
            if (!uop_goes_to_exec(uop))
                idr_uop_delay_out->put(uop);
            ev_uop_rename(uop, uop_pipes(allowed_pipes(uop)));
        }

        // The IDR-FSU interface will stall for one cycle if more than 2 FSU uops were dispatched to either
        // the X or Y pipe
        if (uop_group.get().resource_count[sch_bandwidth_resource_id(Sch::X)] > fsu_schedule_per_cycle) {
            stall_idr_fsu = true;
            stall_idr_fsu_pipe = Pipe::X;
            fsu_schedulers_free = false;
        }

        if (uop_group.get().resource_count[sch_bandwidth_resource_id(Sch::Y)] > fsu_schedule_per_cycle) {
            stall_idr_fsu = true;
            stall_idr_fsu_pipe = Pipe::Y;
            fsu_schedulers_free = false;
        }

        check_self_sync(uop_group.get());

        dispatch(uop_group.get());

        if (uop_group.get().uops[0]->is_post_dispatch_drain()) {
            AMPERE_ASSERT(uop_group.get().uops[0]->is_isolated(), "Post-dispatch drain uops must be isolated");
            waiting_for_rob_empty = true;
            // Give a little time for the ROB to go non-empty if the barrier is the only instruction.
            barrier_reaches_rob = get_cycle() + 2;
        }

        if (uop_group.get().uops[0]->is_post_dispatch_stall()) {
            AMPERE_ASSERT(uop_group.get().uops[0]->is_isolated(), "Post-dispatch stall uops must be isolated");
            waiting_for_flush = true;
        }
    } else {
        if ((!bob_stall_is_backend_stall) && (!resource_vector[buffer_resource_id(BufferResource::BOB)])) {
            // In Furiosa RTL, BOB ID stalls count as frontend stalls and
            // inhibit backend stalls for any cycle in which they occur
            ev_frontend_stall();
            frontend_stall->put(1);
        } else {
            // Note, these events are assigned to the first uop's
            // transaction, but may be reporting the cause for any uop in
            // the group
            // ev_backend_stall is mapped to the STALL_BACKEND_RESOURCE, STALL_BACKEND_CPUBOUND events
            // ev_backend_stall is: STALL_BACKEND - IDR_STALL_DRAIN
            // STALL_BACKEND is computed in rollup_formula.py as ev_backend_stall + IDR_STALL_DRAIN
            ev_backend_stall(uop_group.get().uops[0], stall_cause_data(stall_cause(resource_vector)));
        }

        pmu_backend_events(uop_group.get().uops[0], resource_vector);
        check_for_resources = false;

        // If we're stalled, restore the original value of the Z-pipe pointer -- it won't toggle
        // unless we actually dispatch. Otherwise we might change it multiple times for the same
        // uop group, since we redo pipe assignment every cycle.
        for (auto* assign : assignments)
            assign->stalled();
        return false;
    }

    return true;
}

bool IDR::schedule_jit(const UopGroup& uop_row)
{
    std::vector<Scheduler> old_scheduler;

    std::vector<Credit> credits;
    bool acquired = true;

    for (const auto& uop : uop_row.uops) {
        auto allowed_pipes = uop->allowed_pipes();
        if (!uop->is_executed_in_idr() || execute_idr_uops) {
            // This uop can use the assigned scheduler
            if (sch[uop->sch.scheduler()].can_acquire()) {
                old_scheduler.emplace_back(uop->sch);
                credits.push_back(sch[uop->sch.scheduler()].acquire());
            } else {
                unsigned i = 0;
                for (; i < sch.size(); ++i) {
                    auto s = Scheduler::from_sched(i);
                    auto pipe = pipe_from_sch(*s.sched_type, allowed_pipes);
                    AMPERE_ASSERT_ERROR(pipe != Pipe::INVALID, "Invalid scheduler for allowed pipes");
                    if ((allowed_pipes & static_cast<uint64_t>(pipe)) && sch[i].can_acquire()) {
                        old_scheduler.emplace_back(uop->sch);
                        assign_sch(uop, s);
                        credits.push_back(sch[uop->sch.scheduler()].acquire());
                        break;
                    }
                }
                acquired = i != sch.size();
            }
        } else {
            old_scheduler.emplace_back(uop->sch);
        }

        if (!acquired)
            break;
    }

    if (acquired) {
        assign_pipes(uop_row);
    } else {
        for (unsigned i = 0; i < old_scheduler.size(); i++)
            assign_sch(uop_row.uops[i], old_scheduler[i]);
    }

    for (auto i = credits.begin(); i != credits.end();) {
        release_credit(*i);
        i = credits.erase(i);
    }

    return acquired;
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
