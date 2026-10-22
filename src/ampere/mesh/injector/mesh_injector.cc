//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/mesh/injector/mesh_injector.h"

namespace ampere::mesh {

MeshInjector::MeshInjector(const sc_core::sc_module_name& name, sc::Clock* clock) : MeshModule{name, clock}
{
    num_banks = 1 << bank_hash_bits;
    bank_hash_mask = num_banks - 1;
    m_txn_id_free_list.resize(num_banks);
    m_stash_txn_id_free_list.resize(num_banks);
    AMPERE_ASSERT(max_outstanding_requests % num_banks == 0,
                  "Allowed requests should be evenly divisible by number of banks");
    AMPERE_ASSERT(max_outstanding_stashes % num_banks == 0,
                  "Allowed stashes should be evenly divisible by number of banks");
    for (size_t bank = 0; bank < num_banks; bank++) {
        m_txn_id_free_list[bank].resize(max_outstanding_requests / num_banks);
        m_stash_txn_id_free_list[bank].resize(max_outstanding_stashes / num_banks);
        // Distribute ids such that the low bits identify which bank the id belongs to and high bits
        // differentiate ids within a bank with high-numbered ids corresponding to reserved stash
        // ids
        std::generate(
            m_txn_id_free_list[bank].begin(), m_txn_id_free_list[bank].end(), [n = bank, off = num_banks]() mutable {
                n += off;
                return n - off;
            });
        std::generate(m_stash_txn_id_free_list[bank].begin(),
                      m_stash_txn_id_free_list[bank].end(),
                      [n = bank + max_outstanding_requests, off = num_banks]() mutable {
                          n += off;
                          return n - off;
                      });
    }

    if (enable && !stash_only)
        uop_source->enable();
    else
        uop_source->disable();

    uop_source->set_node_id(node_id);

    l2_pipe_delay_in_cycles = (delay * 1000) / get_clock()->get_period().value();

    rng.seed(kRandSeed);
    udist.param(std::uniform_int_distribution<int>::param_type(dist_start, dist_end));

    AMPERE_ASSERT(duty_cycle_param.val().size() == 2, "duty cycle should be a two-entry list: [num, den]");
    duty_cycle.num = duty_cycle_param.val()[0];
    duty_cycle.den = duty_cycle_param.val()[1];

    if (duty_cycle.num > duty_cycle.den)
        duty_cycle.num = duty_cycle.den;

    AMPERE_ASSERT(duty_cycle.num > 0 && duty_cycle.den > 0, "duty cycle should be positive integers");

    duty_cycle_count = Random::get(0, duty_cycle.den);
}

bool MeshInjector::run()
{
    if (!enable)
        return false;

    receive_snoops();
    receive_responses();
    receive_data();
    send_requests();
    send_responses();
    send_data();

    if (!stash_only) {
        do_execute();
        do_commit();
        do_refill();
    }

    if (dummy_evict) {
        AMPERE_ASSERT(stash_only, "Only tested for stash_only true conditions");
        do_refill();
        emit_dummy_evict();
    }

    do_refill_l3();
    do_writeout();

    if (!stash_only)
        check_for_braindeath();

    if (auto itr = l2_refill_delay_queue.find(get_cycle()); itr != l2_refill_delay_queue.end()) {
        (std::get<0>(itr->second))(std::get<1>(itr->second));
        l2_refill_delay_queue.erase(itr);
    }

    if (auto itr = l3_refill_delay_queue.find(get_cycle()); itr != l3_refill_delay_queue.end()) {
        (std::get<0>(itr->second))(std::get<1>(itr->second));
        l3_refill_delay_queue.erase(itr);
    }

    if (auto itr = snp_fwd_delay_queue.find(get_cycle()); itr != snp_fwd_delay_queue.end()) {
        (std::get<0>(itr->second))(std::get<1>(itr->second));
        snp_fwd_delay_queue.erase(itr);
    }

    if (auto itr = snp_unique_delay_queue.find(get_cycle()); itr != snp_unique_delay_queue.end()) {
        (std::get<0>(itr->second))(std::get<1>(itr->second));
        snp_unique_delay_queue.erase(itr);
    }

    if (auto itr = snp_once_delay_queue.find(get_cycle()); itr != snp_once_delay_queue.end()) {
        (std::get<0>(itr->second))(std::get<1>(itr->second));
        snp_once_delay_queue.erase(itr);
    }

    return true;
}

/** Fetch an instruction from the driver, decode it, and put the uops on the
 *  execution queue. This function is not called spontaneously (from run),
 *  but instead is called on demand from do_execute() when the execution queue
 *  runs dry.
 */
void MeshInjector::do_fetch()
{
    while (commit_q.size() < max_pending_uops && exec_q.size() < max_executing_uops && !pipeline_has_barrier) {
        auto maybe_uop = uop_source->fetch_uop();

        if (!maybe_uop)
            break;

        auto uop = *maybe_uop;

        uop->set_address(uop->address() + data_offset);

        auto [is_split, lo, hi] = split_uop_by_cacheline(uop);

        if (is_split) {
            prepare_uop(*lo);
            prepare_uop(*hi);
        } else
            prepare_uop(uop);

        if (!sched_q.empty()) {
            exec_q.push_back(sched_q.front());
            sched_q.pop_front();
        }
    }
}

std::tuple<bool, std::optional<Uop>, std::optional<Uop>> MeshInjector::split_uop_by_cacheline(Uop uop)
{
    bool is_mem = uop->is_load() || uop->is_store();

    if (!is_mem || uop->address().same_cache_line(uop->address() + uop->mem_datasize() - 1)) {
        return {false, {}, {}};
    } else {
        auto hi_half_address = uop->address().next_line_address();
        unsigned low_size = hi_half_address - uop->address();

        auto archx_lo = std::make_unique<archx::Uop>(*uop->archx_uop);
        auto archx_hi = std::make_unique<archx::Uop>(*uop->archx_uop);

        auto lo = make<Uop>(uop_source->take_next_id(), std::move(archx_lo), uop->archx_instruction);
        lo->set_address(uop->address());
        lo->set_mem_datasize(low_size);

        auto hi = make<Uop>(uop_source->take_next_id(), std::move(archx_hi), uop->archx_instruction);
        hi->set_address(hi_half_address);
        hi->set_mem_datasize(hi->mem_datasize() - low_size);

        if (uop->is_end_inst()) {
            lo->set_end_inst(false);
            hi->set_end_inst(true);
        }

        return {true, lo, hi};
    }
}

void MeshInjector::prepare_uop(Uop uop)
{
    if (setup_dependencies(uop) == 0)
        mark_uop_ready(uop);

    mark_producer(uop, uop->archx_uop->dst);
    mark_producer(uop, uop->archx_uop->dst2);

    if (uop->is_store())
        mark_store(uop, uop->address(), uop->mem_datasize());

    if (uop->ready)
        sched_q.push_back(uop);
    commit_q.push_back(uop);

    if (uop->is_barrier)
        pipeline_has_barrier = true;
}

/** Pop a uop off the execution queue and "execute" it. For non-memory uops,
 *  we just complete them. For loads and stores we have to do some real work.
 *
 *  If the execution queue is empty, call do_fetch() first to try to fill it.
 */
void MeshInjector::do_execute()
{
    int loads_this_cycle = 0;
    int stores_this_cycle = 0;
    int nonmem_this_cycle = 0;

    int max_recycles = 2;

    do_fetch();

    while (max_recycles > 0) {
        if (nothing_ready == false) {
            int ready_count{0};

            if (exec_q.empty()) {
                exec_q.insert(exec_q.begin(), sched_q.begin(), sched_q.end());
                sched_q.clear();
            }
            for (auto it = exec_q.begin(); it != exec_q.end();) {
                auto uop = *it;

                if (!sched_q.empty()) {
                    if (uop->id > sched_q.front()->id) {
                        /* Schedule out of the sched_q since there is an older uop ready there */
                        it = exec_q.insert(it, sched_q.front());
                        sched_q.pop_front();
                        uop = *it;
                    }
                }

                if (!uop->ready) {
                    it = exec_q.erase(it);
                    continue;
                }

                ++it;
                ++ready_count;
                if (cm.is_connected() & uop_source->get_ping_pong_mask()) {
                    if (cm->is_ready(node_id))
                        cm->op_start(node_id, uop_source->get_ping_pong_mask());
                    else if (!cm->is_running(node_id))
                        continue;
                }

                if (uop->nonmem_executing) {
                    if (uop->latency > 0) {
                        --uop->latency;
                        continue;
                    } else {
                        complete_uop(uop);
                        uop->nonmem_executing = false;
                        ev_nonmem_comp();
                    }
                }

                if (uop->is_load()) {
                    if (loads_this_cycle < max_loads_per_cycle) {
                        exec_load(uop);
                        ++loads_this_cycle;
                    }
                } else if (uop->is_store()) {
                    if (stores_this_cycle < max_stores_per_cycle) {
                        exec_store(uop);
                        ++stores_this_cycle;
                    }
                } else {
                    if (nonmem_this_cycle < max_nonmem_per_cycle) {
                        // uop->ready = false;
                        if (uop->is_barrier)
                            waiting_uops.sleep({Block::Code::BARRIER, uop->id}, uop);
                        else {
                            uop->nonmem_executing = true;
                            if (uop->latency > 0) {
                                --uop->latency;
                            }
                        }
                        ++nonmem_this_cycle;
                    }
                }
                nothing_ready = ready_count == 0;
            }
        }

        if (uop_source->get_ping_pong_mask())
            wake_barrier_if_complete();
        remove_completed_uops();

        if (sched_q.empty()) {
            max_recycles = 0;
        } else {
            --max_recycles;
        }
    }
}

/** Remove completed uops from the execution queue.
 */
void MeshInjector::remove_completed_uops()
{
    if (complete_count) {
        for (auto it = exec_q.begin(); it != exec_q.end();) {
            if ((!uop_source->get_ping_pong_mask() && (*it)->complete)
                || (uop_source->get_ping_pong_mask() && (*it)->is_store() && (*it)->str_complete)
                || (uop_source->get_ping_pong_mask() && !(*it)->is_store() && (*it)->complete))
            {
                it = exec_q.erase(it);
                AMPERE_ASSERT(complete_count > 0, "complete_count dropped too low");
                --complete_count;
            } else {
                ++it;
            }
        }
    }
}

/** Mark a uop ready and reset the `nothing_ready` flag.
 */
void MeshInjector::mark_uop_ready(Uop& uop)
{
    uop->ready = true;
    nothing_ready = false;
}

void MeshInjector::schedule_uop(Uop& uop)
{
    mark_uop_ready(uop);
    sched_q.push_back(uop);
}

void MeshInjector::wake_barrier_if_complete()
{
    std::optional<Uop> barrier_uop;

    for (auto& uop : exec_q) {
        if (uop->is_store()) { // stores are complete on `str_complete`
            if (uop->str_complete)
                continue;
        } else if (uop->complete) { // all other uops are complete on `complete`
            continue;
        }

        if (uop->is_barrier) {
            barrier_uop = uop;
            break;
        }
        return;
    }

    // reached here that means all uops before barrier have truely completed
    // thus wake up barrier uop
    if (barrier_uop) {
        waiting_uops.wake({Block::Code::BARRIER, barrier_uop.value()->id}, [this](Uop u) { schedule_uop(u); });
        if (cm.is_connected())
            cm->op_done(node_id, uop_source->get_ping_pong_mask());
        complete_uop(barrier_uop.value());
        pipeline_has_barrier = false;
        ev_barrier_comp();
    }
}

/** Ensure that a uop is marked dependent upon its sources.
 *
 * @param uop
 * @return The block count (the number of sources we're waiting for)
 */
int MeshInjector::setup_dependencies(Uop uop)
{
    mark_consumer(uop, uop->archx_uop->src1);
    mark_consumer(uop, uop->archx_uop->src2);
    mark_consumer(uop, uop->archx_uop->src3);

    if (uop->is_load())
        mark_load(uop, uop->address(), uop->mem_datasize());

    return uop->block_count;
}

/** Block a uop (maybe) on a source register. If the source currently exists in
 *  the producers table, that means there is a uop yet to be executed which
 *  generates that source, and we have to wait.
 *
 * @param uop
 * @param src
 */
void MeshInjector::mark_consumer(Uop uop, LReg src)
{
    auto src_dep = producers.find(src);

    if (src_dep != producers.end())
        waiting_uops.sleep({Block::Code::REGISTER, src_dep->second->id}, uop);
}

/** Add a destination uop to the producers table for a given destination register.
 *  This only makes sense if the destination register is "real".
 *
 * @param uop
 * @param dst
 */
void MeshInjector::mark_producer(Uop uop, LReg dst)
{
    if (dst != LReg::INVALID && dst != LReg::ZR)
        producers[dst] = uop;
}

/** Record a store's address and size in the store map so we can detect memory
 *  dependencies.
 *
 *  @param uop  The store uop
 *  @param address Physical address of the store
 *  @param size Size of the store (bytes)
 */
void MeshInjector::mark_store(Uop uop, PhysAddr address, unsigned size)
{
    auto lo = address.extract_range(5, 0);
    auto mask = util::mask(lo + size - 1, lo);
    stores.insert({address.line_address(), {uop->id, mask}});
}

/** Block a uop (maybe) on a previous store (or stores). Stores which have not yet
 *  completed (in the sense of actually writing to the cache, not committing, which
 *  happens early for a store) will be in the stores table. If we find any overlaps
 *  with the byte mask, then we have a R-A-W dependency.
 *
 * @param uop A load uop
 * @param address The load address
 * @param size The size of the load (in bytes)
 */
void MeshInjector::mark_load(Uop uop, PhysAddr address, unsigned size)
{
    auto lo = address.extract_range(5, 0);
    auto mask = util::mask(lo + size - 1, lo);

    auto [p, q] = stores.equal_range(address.line_address());

    while (p != q) {
        if (p->second.second & mask)
            waiting_uops.sleep({Block::Code::ADDRESS, p->second.first}, uop);
        ++p;
    }
}

/** Remove a uop from the producers table (once it is complete). Of course we may
 *  have moved on and a subsequent uop is now the producer of that same register,
 *  so we have to check that the producer is still the given uop.
 *
 * @param uop
 * @param dst
 */
void MeshInjector::unmark_producer(Uop uop, LReg dst)
{
    auto p = producers.find(dst);

    if (p != producers.end() && p->second == uop)
        producers.erase(p);
}

/** Remove a store from the stores table (once it is complete).
 *
 * @param uop
 * @param address
 */
void MeshInjector::unmark_store(Uop uop, PhysAddr address, unsigned size)
{
    auto [p, q] = stores.equal_range(address.line_address());

    while (p != q) {
        if (p->second.first == uop->id)
            p = stores.erase(p);
        else
            ++p;
    }
}

/** Commit a uop at the head of the commit queue if it is complete. If we reach
 *  an instruction boundary, commit the instruction in the driver as well.
 */
void MeshInjector::do_commit()
{
    int commits_this_cycle = 0;

    while (!commit_q.empty() && commits_this_cycle < max_commits_per_cycle) {
        auto uop = commit_q.front();

        if (uop->complete) {
            commit_q.pop_front();
            last_commit_cycle = get_cycle();

            unsigned bytes_loaded = 0;
            unsigned bytes_stored = 0;

            if (uop->is_load() && !uop->archx_uop->ldp_2nd_uop)
                bytes_loaded = uop->mem_datasize();

            if (uop->is_store()) // Is there some concern with store pairs?
                bytes_stored = uop->mem_datasize();

            ev_uop_commit(evd_bytes_loaded(bytes_loaded), evd_bytes_stored(bytes_stored));

            if (uop->is_end_inst()) {
                ev_instruction_commit();
                ++instructions;

                if (heartbeat && (instructions % heartbeat) == 0)
                    fmt::print(std::cerr, "{}: committed {} instructions\n", sc_core::sc_time_stamp(), instructions);
            }

            uop_source->commit_uop(uop);
            ++commits_this_cycle;
        } else {
            break;
        }
    }
}

/** Execute a load by looking it up in the cache. If it hits, we're done. Otherwise
 *  we need to generate a request to the mesh.
 *
 * @param uop
 */
void MeshInjector::exec_load(Uop uop)
{
    uop->ready = false;

    auto hit_l2 = cache.lookup(uop, uop->address());
    auto hit_l3 = l3c.lookup(uop, uop->address());

    ev_load_exec(uop);
    if (hit_l3)
        ev_hit_l3();

    if (!hit_l2 && !hit_l3)
        make_request(
            uop,
            read_with_read_not_shared_dirty ? chi::RequestOpcode::ReadNotSharedDirty : chi::RequestOpcode::ReadShared);
    else
        complete_uop(uop);
}

/** Execute a store by looking it up in the cache. If we miss or we don't have the line
 *  in unique state, then we'll need to generate a request to the mesh. Stores are generally
 *  complete at this point, doing most of their work after commit, unless they failed to
 *  make a request.
 *
 * @param uop
 */
void MeshInjector::exec_store(Uop uop)
{
    uop->ready = false;

    auto hit_l2 = cache.lookup(uop, uop->address());
    auto hit_l3 = l3c.lookup(uop, uop->address());

    ev_store_exec();

    if (hit_l3)
        ev_hit_l3();

    if (!hit_l2 && !hit_l3) {
        chi::RequestOpcode opcode =
            ((uop->mem_datasize() == 64) ? chi::RequestOpcode::MakeUnique : chi::RequestOpcode::ReadUnique);

        if (make_request(uop, opcode))
            complete_uop(uop);
    } else if ((!hit_l2 && (hit_l3 && !hit_l3->is_unique())) || (hit_l2 && !hit_l2->is_unique())) {
        if (make_request(uop, chi::RequestOpcode::CleanUnique))
            complete_uop(uop);
    } else {
        if (hit_l2) {
            AMPERE_ASSERT(hit_l2->is_unique(), "Line should be in unique state to execute store");
            hit_l2->state = CacheEntry::State::UD;
        }
        if (hit_l3) {
            AMPERE_ASSERT(hit_l3->is_unique(), "Line should be in unique state to execute store");
            hit_l3->state = CacheEntry::State::UD;
        }
        complete_uop(uop);
    }
}

/** Execute a non-memory operation, which we do simply by completing it.
 *
 *  @param uop
 */
void MeshInjector::exec_nonmem(Uop uop)
{
    uop->ready = false;
    if (uop->is_barrier)
        waiting_uops.sleep({Block::Code::BARRIER, uop->id}, uop);
    else
        complete_uop(uop);
}

/** Complete the execution of a uop. This means removing its destinations (maybe) from the
 *  producers table, as well as waking up any uops that were blocked on it. After this the
 *  uop can be committed.
 *
 * @param uop
 */
void MeshInjector::complete_uop(Uop uop)
{
    AMPERE_ASSERT(!uop->complete, "uop is already complete");

    unmark_producer(uop, uop->archx_uop->dst);
    unmark_producer(uop, uop->archx_uop->dst2);

    waiting_uops.wake({Block::Code::REGISTER, uop->id}, [this](Uop u) { schedule_uop(u); });

    if (uop->is_store()) {
        unmark_store(uop, uop->address(), uop->mem_datasize());
        waiting_uops.wake({Block::Code::ADDRESS, uop->id}, [this](Uop u) { schedule_uop(u); });
    }

    uop->complete = true;
    ++complete_count;
}

void MeshInjector::preload_in_l2(PhysAddr addr, MeshInjector::CacheEntry::State state)
{
    auto dummy_uop = make<Txn>();
    cache.refill(dummy_uop, addr, CacheEntry{addr, state}, true);
}

void MeshInjector::preload_in_l3(PhysAddr addr, MeshInjector::CacheEntry::State state)
{
    auto dummy_uop = make<Txn>();
    l3c.refill(dummy_uop, addr, CacheEntry{addr, state}, true);
}

MeshInjector::CacheEntry::State MeshInjector::peek_state_l2(PhysAddr addr)
{
    auto dummy_uop = make<Txn>();
    auto entry = cache.lookup(dummy_uop, addr, false, true, false, 0);
    return entry ? entry->state : MeshInjector::CacheEntry::State::I;
}

MeshInjector::CacheEntry::State MeshInjector::peek_state_l3(PhysAddr addr)
{
    auto dummy_uop = make<Txn>();
    auto entry = l3c.lookup(dummy_uop, addr, false, true, false, 0);
    return entry ? entry->state : MeshInjector::CacheEntry::State::I;
}

void MeshInjector::l3_refill(MemReq req)
{
    free_txn_id(req->dat->TxnID);
    if (req->dat->Resp.state() != chi::FinalState::I) {
        auto state = chi_state_to_cache_state(req->dat->Resp.state(), req->dat->Resp.pass_dirty());
        auto dummy_uop = make<Txn>();
        auto victim = l3c.refill(dummy_uop, req->snp->Addr, CacheEntry{req->snp->Addr, state});

        ev_l3c_refill(req->snp, req->snp->Addr);

        if (victim) {
            switch (victim->state) {
            case CacheEntry::State::I:
                break;
            case CacheEntry::State::UC:
            case CacheEntry::State::SC:
                make_eviction(req, victim->addr, victim->state, chi::RequestOpcode::Evict, false);
                break;
            case CacheEntry::State::SD:
            case CacheEntry::State::UD:
                make_eviction(req, victim->addr, victim->state, chi::RequestOpcode::WriteBackFull, false);
                break;
            }
            ev_l3_evict();
        }
    }
}

void MeshInjector::do_refill_l3()
{
    if (!refill_l3_q.empty()) {
        auto req = refill_l3_q.front();
        refill_l3_q.pop_front();

        l3_refill_delay_queue.emplace(get_cycle() + l2_pipe_delay_in_cycles,
                                      std::make_tuple(std::bind_front(&MeshInjector::l3_refill, this), req));
    }
}

void MeshInjector::dump_l2(std::ostream& os)
{
    size_t set_num = 0;

    fmt::print(os, "{} L2:\n", name());

    for (auto& set : cache.get_sets()) {
        if (!set.empty()) {
            fmt::print(os, "set {}:", set_num++);

            for (auto& [tag, entry] : set) {
                fmt::print(os, " addr:{} state:{}", entry.value.addr, entry.value.state);
            }

            fmt::print(os, "\n");
        }
    }
}

void MeshInjector::dump_l3(std::ostream& os)
{
    size_t set_num = 0;

    fmt::print(os, "{} L3:\n", name());

    for (auto& set : l3c.get_sets()) {
        if (!set.empty()) {
            fmt::print(os, "set {}:", set_num++);

            for (auto& [tag, entry] : set) {
                fmt::print(os, " addr:{} state:{}", entry.value.addr, entry.value.state);
            }

            fmt::print(os, "\n");
        }
    }
}

int MeshInjector::l2c_occupancy()
{
    int count = 0;
    for (auto& set : cache.get_sets()) {
        count += set.size();
    }
    return count;
}

int MeshInjector::l3c_occupancy()
{
    int count = 0;
    for (auto& set : l3c.get_sets()) {
        count += set.size();
    }
    return count;
}

void MeshInjector::l2_refill(MemReq req)
{
    free_txn_id(req->req->TxnID);
    FinalState cache_state;

    if (!req->dat || req->dat->Opcode == chi::DataOpcode::DataSepResp) {
        cache_state = req->rsp->Resp.state(); // RespSepData or Comp
    } else {
        AMPERE_ASSERT(req->dat->Opcode == chi::DataOpcode::CompData, "expected a CompData");
        cache_state = req->dat->Resp.state();
    }

    if (req->req->Opcode == chi::RequestOpcode::CleanUnique || req->req->Opcode == chi::RequestOpcode::MakeUnique) {
        auto hit_l3 = l3c.lookup(req->req, req->req->Addr, false);
        if (hit_l3)
            l3c.invalidate(req->req, req->req->Addr);
    } else if (cache_state != chi::FinalState::I) {
        ev_l2c_refill(req->req, req->req->Addr);
    }

    if (cache_state != chi::FinalState::I) {
        auto state = chi_state_to_cache_state(cache_state, req->uop->is_store());
        auto victim = cache.refill(req->uop, req->req->Addr, CacheEntry{req->req->Addr, state});

        if (dummy_evict) {
            // ignore any victim; invalidate the cacheline just filled and select it for eviction
            cache.invalidate(req->req, req->req->Addr);
            switch (state) {
            case CacheEntry::State::I:
                break;
            case CacheEntry::State::UC:
                if (evict_uc_with_evict)
                    make_eviction(req, req->req->Addr, state, chi::RequestOpcode::Evict, true);
                else
                    make_eviction(req, req->req->Addr, state, chi::RequestOpcode::WriteEvictOrEvict, true);
                break;
            case CacheEntry::State::SC:
                make_eviction(req, req->req->Addr, state, chi::RequestOpcode::Evict, true);
                break;
            case CacheEntry::State::SD:
                AMPERE_FATAL("not expecting a cache line in SD state");
                break;
            case CacheEntry::State::UD:
                make_eviction(req, req->req->Addr, state, chi::RequestOpcode::WriteBackFull, true);
                break;
            }

            ev_evict_dummy();
        }

        else if (victim)
        {
            switch (victim->state) {
            case CacheEntry::State::I:
                break;
            case CacheEntry::State::UC:
                if (evict_uc_with_evict)
                    make_eviction(req, victim->addr, victim->state, chi::RequestOpcode::Evict, true);
                else
                    make_eviction(req, victim->addr, victim->state, chi::RequestOpcode::WriteEvictOrEvict, true);
                break;
            case CacheEntry::State::SC:
                make_eviction(req, victim->addr, victim->state, chi::RequestOpcode::Evict, true);
                break;
            case CacheEntry::State::SD:
                AMPERE_FATAL("not expecting a cache line in SD state");
                break;
            case CacheEntry::State::UD:
                make_eviction(req, victim->addr, victim->state, chi::RequestOpcode::WriteBackFull, true);
                ev_l2c_writeback(req->req, victim->addr);
                break;
            }
            ev_l2_evict();
        }

        if (req->uop->is_store()) {
            AMPERE_ASSERT(req->uop->complete == true, "Store should be complete by now");
            req->uop->str_complete = true;
        }
    }

    if (!req->uop->complete)
        complete_uop(req->uop);

    pending_reqs.erase(req->req->Addr);
    waiting_uops.wake({Block::Code::SAME_CACHE_LINE, req->req->Addr.value()}, [this](Uop u) {
        schedule_uop(u);
        if (u->is_store())
            u->str_complete = true;
    });
}

/** Refill the cache with data coming back from the mesh. This represents the completion
 *  of a load, but a store that needed to refill is already complete.
 *
 *  TODO: Right now this function is handling all manner of responses from the mesh. Likely it will need breaking up.
 */
void MeshInjector::do_refill()
{
    if (!refill_q.empty()) {
        auto req = refill_q.front();
        refill_q.pop_front();
        l2_refill_delay_queue.emplace(get_cycle() + l2_pipe_delay_in_cycles,
                                      std::make_tuple(std::bind_front(&MeshInjector::l2_refill, this), req));
    }
}

auto MeshInjector::compute_snoop_state(MeshSnp snp, bool is_l3, CacheEntry::State current_state)
    -> MeshInjector::SnpState
{
    if (snp->Opcode == chi::SnoopOpcode::SnpSharedFwd) {
        if (!(is_l3 && snp->InvL3Copy)) {
            switch (current_state) {
            case CacheEntry::State::I:
                return {CacheEntry::State::I, chi::RespSnoop_I};
            case CacheEntry::State::UC:
                return {CacheEntry::State::SC, chi::RespSnoop_SC, chi::RespComp_SC};
            case CacheEntry::State::UD:
                return {CacheEntry::State::SC, chi::RespSnoop_SC, chi::RespComp_SD_PD};
            case CacheEntry::State::SC:
                return {CacheEntry::State::SC, chi::RespSnoop_SC, chi::RespComp_SC};
            case CacheEntry::State::SD:
                return {CacheEntry::State::SC, chi::RespSnoop_SC, chi::RespComp_SD_PD};
            }
        } else { // L3, with invalidation
            switch (current_state) {
            case CacheEntry::State::I:
                return {CacheEntry::State::I, chi::RespSnoop_I};
            case CacheEntry::State::UC:
                return {CacheEntry::State::I, chi::RespSnoop_I, chi::RespComp_UC};
            case CacheEntry::State::UD:
                return {CacheEntry::State::I, chi::RespSnoop_I, chi::RespComp_UD_PD};
            case CacheEntry::State::SC:
                return {CacheEntry::State::I, chi::RespSnoop_I, chi::RespComp_SC};
            case CacheEntry::State::SD:
                return {CacheEntry::State::I, chi::RespSnoop_I, chi::RespComp_SD_PD};
            }
        }
    } else if (snp->Opcode == chi::SnoopOpcode::SnpUniqueFwd) {
        switch (current_state) {
        case CacheEntry::State::I:
            return {CacheEntry::State::I, chi::RespSnoop_I};
        case CacheEntry::State::UC:
            return {CacheEntry::State::I, chi::RespSnoop_I, chi::RespComp_UC};
        case CacheEntry::State::UD:
            return {CacheEntry::State::I, chi::RespSnoop_I, chi::RespComp_UD_PD};
        case CacheEntry::State::SC:
            return {CacheEntry::State::I, chi::RespSnoop_I, chi::RespComp_UC};
        case CacheEntry::State::SD:
            return {CacheEntry::State::I, chi::RespSnoop_I, chi::RespComp_UD_PD};
        }
    } else if (snp->Opcode == chi::SnoopOpcode::SnpCleanFwd) {
        switch (current_state) {
        case CacheEntry::State::I:
            return {CacheEntry::State::I, chi::RespSnoop_I};
        case CacheEntry::State::UC:
            return {CacheEntry::State::SC, chi::RespSnoop_SC, chi::RespComp_SC};
        case CacheEntry::State::UD:
            return {CacheEntry::State::SD, chi::RespSnoop_SD, chi::RespComp_SC};
        case CacheEntry::State::SC:
            return {CacheEntry::State::SC, chi::RespSnoop_SC, chi::RespComp_SC};
        case CacheEntry::State::SD:
            return {CacheEntry::State::SD, chi::RespSnoop_SD, chi::RespComp_SC};
        }
    } else if (snp->Opcode == chi::SnoopOpcode::SnpOnce) {
        switch (current_state) {
        case CacheEntry::State::I:
            return {CacheEntry::State::I, chi::RespSnoop_I};
        case CacheEntry::State::UC:
            return {CacheEntry::State::UC, chi::RespSnoop_UC, chi::RespComp_I};
        case CacheEntry::State::UD:
            return {CacheEntry::State::UD, chi::RespSnoop_UD, chi::RespComp_I};
        case CacheEntry::State::SC:
            return {CacheEntry::State::SC, chi::RespSnoop_SC, chi::RespComp_I};
        case CacheEntry::State::SD:
            return {CacheEntry::State::SD, chi::RespSnoop_SD, chi::RespComp_I};
        }
    }
    AMPERE_FATAL("unhandled Snp opcode {}", snp->Opcode);
    return {CacheEntry::State::I, chi::RespSnoop_I};
}

void MeshInjector::do_snp_fwd(MeshSnp snp)
{
    auto hit = cache.lookup(snp, snp->Addr, false);
    auto hit_l3 = l3c.lookup(snp, snp->Addr, false);
    auto fb = fb_lookup(snp->Addr);

    AMPERE_ASSERT(!(hit && hit_l3), "Snoop hit in both L2 and L3");

    auto current_state = hit ? hit->state : hit_l3 ? hit_l3->state : fb ? fb->state : CacheEntry::State::I;

    // FIXME: a FB hit can also be "L3", but we aren't tracking that
    auto status = compute_snoop_state(snp, hit_l3.has_value(), current_state);

    if (hit)
        hit->state = status.cache_state;
    else if (hit_l3)
        hit_l3->state = status.cache_state;
    else if (fb)
        pending_evictions.at(snp->Addr) = status.cache_state;

    if (!(status.fwd_state == chi::RespComp_I)) {
        auto rsp = make_clone<MeshRsp>(snp, node_id, snp->SrcID, snp->TxnID, chi::ResponseOpcode::SnpRespFwded);
        rsp->Resp = status.resp_state;
        rsp->FwdState = status.fwd_state;
        response_q.push_back(rsp);

        make_data_response(snp, chi::DataOpcode::CompData, status.fwd_state);
    } else {
        auto rsp = make_clone<MeshRsp>(snp, node_id, snp->SrcID, snp->TxnID, chi::ResponseOpcode::SnpResp);
        rsp->Resp = status.resp_state;
        response_q.push_back(rsp);
    }
}

void MeshInjector::do_snp_unique(MeshSnp snp)
{
    auto hit = cache.lookup(snp, snp->Addr, false);
    auto hit_l3 = l3c.lookup(snp, snp->Addr, false);
    auto fb = fb_lookup(snp->Addr);
    AMPERE_ASSERT(!(hit && hit_l3 && fb), "Snoop hit in both L2 and L3");

    bool pass_dirty = (hit && hit->is_dirty()) || (hit_l3 && hit_l3->is_dirty()) || (fb && fb->is_dirty());

    if (hit)
        cache.invalidate(snp, snp->Addr);
    else if (hit_l3)
        l3c.invalidate(snp, snp->Addr);
    else if (fb)
        pending_evictions.at(snp->Addr) = CacheEntry::State::I;

    if (hit || hit_l3 || fb) {
        auto pkts = make_data(snp, node_id, snp->SrcID, snp->TxnID, chi::DataOpcode::SnpRespData);
        for (auto& dat : pkts) {
            dat->Resp = pass_dirty ? chi::RespSnoop_I_PD : chi::RespSnoop_I;
            dat->DBID = snp->TxnID;
            dat->HomeNID = snp->SrcID;
            data_q.push_back(dat);
        }
    } else {
        auto rsp = make_clone<MeshRsp>(snp, node_id, snp->SrcID, snp->TxnID, chi::ResponseOpcode::SnpResp);
        rsp->Resp = chi::RespSnoop_I;
        response_q.push_back(rsp);
    }
}

void MeshInjector::do_snp_once(MeshSnp snp)
{
    auto hit = cache.lookup(snp, snp->Addr, false);
    auto hit_l3 = l3c.lookup(snp, snp->Addr, false);
    auto fb = fb_lookup(snp->Addr);
    AMPERE_ASSERT(!(hit && hit_l3 && fb), "Snoop hit in both L2 and L3");

    auto current_state = hit ? hit->state : hit_l3 ? hit_l3->state : fb ? fb->state : CacheEntry::State::I;

    // FIXME: a FB hit can also be "L3", but we aren't tracking that
    auto status = compute_snoop_state(snp, hit_l3.has_value(), current_state);

    if (hit)
        hit->state = status.cache_state;
    else if (hit_l3)
        hit_l3->state = status.cache_state;
    else if (fb)
        pending_evictions.at(snp->Addr) = status.cache_state;

    if (snp->RetToSrc || status.resp_state == chi::RespSnoop_UD || status.resp_state == chi::RespSnoop_SD)
        make_data_response(snp, chi::DataOpcode::SnpRespData, status.resp_state);
    else {
        auto rsp = make_clone<MeshRsp>(snp, node_id, snp->SrcID, snp->TxnID, chi::ResponseOpcode::SnpResp);
        rsp->Resp = status.resp_state;
        response_q.push_back(rsp);
    }
}

/** Send data out to the mesh (for evictions) if there's anything on the writeout queue.
 */
void MeshInjector::do_writeout()
{
    if (!writeout_q.empty()) {
        auto mem_req = writeout_q.front();
        writeout_q.pop_front();
        free_txn_id(mem_req->req->TxnID);

        auto fb = pending_evictions.find(mem_req->req->Addr);
        AMPERE_ASSERT(fb != pending_evictions.end(),
                      "Attempting to response with data of a cacheline we don't have in pending_evictions");
        chi::ResponseStatus resp_status{chi::ResponseStatus::Type::WriteData, cache_state_to_chi_state(fb->second)};
        pending_evictions.erase(fb);
        make_data_response(mem_req->rsp, chi::DataOpcode::CopyBackWrData, resp_status);
    }
}

/** Create a request and put it on the request queue to go out. We can't create a request
 *  if there isn't an available transaction id (i.e., we've got too many outstanding transactions),
 *  so in that case put the uop to sleep.
 *
 * @param uop Uop that's generating this request (will become the parent transaction of the MemReq)
 * @param opcode The request opcode to generate.
 */
bool MeshInjector::make_request(Uop uop, chi::RequestOpcode opcode)
{
    auto address = uop->address().line_address();

    if (pending_reqs.contains(uop->address().line_address())) {
        waiting_uops.sleep({Block::Code::SAME_CACHE_LINE, address.value()}, uop);
        return false;
    } else {
        auto bank = select_bank(uop->address());
        auto maybe_txn_id = alloc_txn_id(bank);

        if (!maybe_txn_id.has_value()) {
            waiting_uops.sleep({Block::Code::REQUEST_ID, bank}, uop);
            return false;
        }

        auto txn_id = *maybe_txn_id;
        chi::NodeId dest_nodeid;

        dest_nodeid = sam->lookup(address);

        auto req = boost::make_local_shared<MemReqObj>(
            uop, make_child<MeshReq>(uop, node_id, dest_nodeid, txn_id, opcode, address));

        if (chi::is_read(req->req->Opcode)
            && ((outstanding_requests - qos_demote_untracked_txns.size()) < cbusy->qos_demote_limit()))
            req->req->QoS = chi::Qos::Level::PLATINUM;
        else
            req->req->QoS = chi::Qos::Level::GOLD;

        request_q.push_back(req);
        pending_reqs.insert(address);

        return true;
    }
}

/** Create an eviction request and put it on the request queue. This differs from an ordinary
 *  request in that it is not associated with a uop, but with a prior memory request (the one
 *  that kicked it out of the cache).
 *
 *  Note that we should always have an available transaction id for an eviction because we
 *  just freed the one for the evictor. We might not get that one, but we are guaranteed to
 *  have something on the free list.
 *
 * @param evictor The memory request which caused the eviction (will become the parent transaction)
 * @param address The cache line address being booted out.
 * @param opcode Which opcode to generate (e.g., WriteBackFull, Evict, etc.)
 */
void MeshInjector::make_eviction(
    MemReq evictor, PhysAddr address, CacheEntry::State state, chi::RequestOpcode opcode, bool evict_from_l2)
{
    auto maybe_txn_id = alloc_txn_id(select_bank(address), !evict_from_l2);
    AMPERE_ASSERT(maybe_txn_id.has_value(),
                  "no request id available for eviction on bank {}, alloc type {}",
                  select_bank(address),
                  !evict_from_l2 ? "demand" : "stash");

    auto txn_id = *maybe_txn_id;

    chi::NodeId dest_nodeid;
    dest_nodeid = sam->lookup(address);

    MemReq req;
    if (evictor->stash_in_l3)
        req = boost::make_local_shared<MemReqObj>(
            make_child<MeshReq>(evictor->snp, node_id, dest_nodeid, txn_id, opcode, address));
    else
        req = boost::make_local_shared<MemReqObj>(
            make_child<MeshReq>(evictor->req, node_id, dest_nodeid, txn_id, opcode, address));

    if (evict_from_l2) {
        if (opcode == chi::RequestOpcode::WriteBackFull)
            ev_writeback(req->req, req->req->Addr);
    } else {
        req->req->RSVDC = chi::ReserveCustomerUseReq(chi::RsvdcReq::CopyBackL2L3);
    }

    pending_evictions[address] = state;
    request_q.push_back(req);
}

/** Create a response and put it on the response queue.
 *
 * @param req The request which prompted this response.
 * @param opcode The response opcode.
 */
void MeshInjector::make_response(MemReq req, chi::ResponseOpcode opcode, uint64_t dbid)
{
    // FIXME: The DBID as transaction ID might only be true for CompAck
    MeshRsp rsp;
    if (req->stash_in_l3)
        rsp = make_clone<MeshRsp>(req->snp, node_id, req->snp->SrcID, req->snp->TxnID, opcode);
    else
        rsp = make_clone<MeshRsp>(req->req, node_id, req->req->TgtID, dbid, opcode);
    response_q.push_back(rsp);
}

/** Create a snoop response and put it on the response queue.
 *
 * @param snp The snoop request which prompted this response.
 * @param opcode The response opcode.
 */
MeshRsp MeshInjector::make_response(MeshSnp snp, chi::ResponseOpcode opcode)
{
    auto rsp = make_clone<MeshRsp>(snp, node_id, snp->FwdNID, snp->FwdTxnID, opcode);
    rsp->Resp = chi::RespSnoop_I; // FIXME: obviously not always the case
    response_q.push_back(rsp);
    return rsp;
}

/** Create a data packet and put it on the data queue.
 *
 * @param rsp The parent request which led to this data being sent.
 */
void MeshInjector::make_data_response(MeshRsp rsp, chi::DataOpcode opcode, chi::ResponseStatus resp)
{
    auto pkts = make_data(rsp, node_id, rsp->SrcID, rsp->DBID, opcode);

    for (auto& dat : pkts) {
        dat->Resp = resp;
        dat->DBID = rsp->TxnID;
        dat->HomeNID = rsp->SrcID;
        data_q.push_back(dat);
    }
}

/** Create a data packet and put it on the data queue. This variation is used for snoop
 *  responses that require data.
 *
 * @param snp The parent snoop request which led to this data being sent.
 */
void MeshInjector::make_data_response(MeshSnp snp, chi::DataOpcode opcode, chi::ResponseStatus resp)
{
    auto pkts = make_data(snp, node_id, snp->FwdNID, snp->FwdTxnID, opcode);

    for (auto& dat : pkts) {
        dat->Resp = resp;
        dat->DBID = snp->TxnID;
        dat->HomeNID = snp->SrcID;
        data_q.push_back(dat);
    }
}

size_t MeshInjector::select_bank(PhysAddr addr)
{
    switch (num_banks) {
    case 8:
        return addr.extract_range(8, 6) ^ addr.extract_range(11, 9) ^ addr.extract_range(14, 12);
    case 4:
        return addr.extract_range(7, 6) ^ addr.extract_range(9, 8) ^ addr.extract_range(11, 10)
               ^ addr.extract_range(13, 12);
    case 1:
        return 0;
    default:
        return addr.line_number() & bank_hash_mask;
    }
}

/** Allocate a transaction id from the free list.
 *
 *  @return A an optional transaction id; set if the free list is not empty.
 */
std::optional<uint16_t> MeshInjector::alloc_txn_id(size_t bank, bool stash)
{
    if (stash && !m_stash_txn_id_free_list[bank].empty()) {
        auto txn_id = m_stash_txn_id_free_list[bank].front();
        m_stash_txn_id_free_list[bank].pop_front();
        ev_stash_txn_id_alloc(evd_txnid(txn_id));
        return txn_id;
    } else if (!m_txn_id_free_list[bank].empty()) {
        auto txn_id = m_txn_id_free_list[bank].front();
        m_txn_id_free_list[bank].pop_front();
        ev_txn_id_alloc(evd_txnid(txn_id));
        return txn_id;
    } else {
        return std::nullopt;
    }
}

/** Return a transaction id to the free list and wake up any and all uops that are waiting
 *  for a request id. It's possible they might try to execute and fail again, but at least
 *  one should succeed.
 *
 * @param txn_id
 */
void MeshInjector::free_txn_id(uint16_t txn_id)
{
    size_t bank = txn_id & bank_hash_mask;

    qos_demote_untracked_txns.erase(txn_id);
    throttle_untracked_txns.erase(txn_id);

    if (txn_id >= max_outstanding_requests) {
        m_stash_txn_id_free_list[bank].push_back(txn_id);
        ev_stash_txn_id_dealloc(evd_txnid(txn_id));
    } else {
        m_txn_id_free_list[bank].push_back(txn_id);
        ev_txn_id_dealloc(evd_txnid(txn_id));
        waiting_uops.wake({Block::Code::REQUEST_ID, bank}, [this](Uop uop) { schedule_uop(uop); });
    }
}

/** Send a request to the mesh if possible. The request will then go to sleep waiting on
 *  as response.
 */
void MeshInjector::send_requests()
{
    if ((send_this_cycle() && !request_q.empty()) || !priority_request_q.empty()) {
        if (mesh_port.tx().req[0]->can_put()) {
            auto memreq = priority_request_q.empty() ? request_q.front() : priority_request_q.front();
            auto req = memreq->req;
            mesh_port.tx().req[0]->put(req);
            ev_send_req(req,
                        req->Opcode,
                        req->Addr,
                        evd_srcid(req->SrcID.id()),
                        evd_tgtid(req->TgtID.id()),
                        evd_txnid(req->TxnID),
                        evd_qos(req->QoS.to_level()),
                        evd_outstanding(waiting_requests.sleep_count(Block::Code::MEM_RESPONSE)),
                        evd_pcrd(waiting_requests.sleep_count(Block::Code::PCREDIT)),
                        evd_allow_retry(req->AllowRetry));
            if (priority_request_q.empty()) {
                request_q.pop_front();
            } else {
                priority_request_q.pop_front();
            }
            waiting_requests.sleep({Block::Code::MEM_RESPONSE, req->TxnID}, memreq);
            ++outstanding_requests;
            if (req->RSVDC == chi::ReserveCustomerUseReq(chi::RsvdcReq::CopyBackL2L3)) {
                bool untrack_success{true};
                if (!l3_evictions_demote_qos)
                    untrack_success = qos_demote_untracked_txns.insert(req->TxnID).second && untrack_success;
                if (!l3_evictions_throttle)
                    untrack_success = throttle_untracked_txns.insert(req->TxnID).second && untrack_success;

                AMPERE_ASSERT(untrack_success, "TxnID {} could not be associated with L3 eviction", req->TxnID);
            }
        } else {
            ev_reject();
        }
    }
}

bool MeshInjector::send_this_cycle()
{
    if (duty_cycle_count + duty_cycle.num > 0)
        duty_cycle_count += duty_cycle.num;

    if ((outstanding_requests - throttle_untracked_txns.size()) < cbusy->num_requests()
        && duty_cycle_count >= duty_cycle.den)
    {
        duty_cycle_count -= duty_cycle.den;
        return true;
    }

    return false;
}

/** Send a response to the mesh if possible.
 */
void MeshInjector::send_responses()
{
    int chan = 0;

    if (num_rsp_ports == 2 && !response_q.empty()) {
        auto& rsp = response_q.front();
        switch (rsp->Opcode) {
        case chi::ResponseOpcode::SnpResp:
        case chi::ResponseOpcode::CompAck:
        case chi::ResponseOpcode::DBIDResp:
        case chi::ResponseOpcode::ReadReceipt:
        case chi::ResponseOpcode::SnpRespFwded:
            chan = 1;
            break;
        default:
            break;
        }
    }

    if (!response_q.empty() && mesh_port.tx().rsp[chan]->can_put()) {
        auto rsp = response_q.front();
        mesh_port.tx().rsp[chan]->put(rsp);
        response_q.pop_front();
        ev_send_rsp(rsp,
                    rsp->Opcode,
                    evd_srcid(rsp->SrcID.id()),
                    evd_tgtid(rsp->TgtID.id()),
                    evd_txnid(rsp->TxnID),
                    evd_data_pull(rsp->DatPull == DataPull::Read));
    }
}

std::optional<MeshInjector::CacheEntry> MeshInjector::fb_lookup(PhysAddr addr)
{
    auto fb = pending_evictions.find(addr);
    if (fb == pending_evictions.end())
        return std::nullopt;

    CacheEntry ce = CacheEntry{fb->first, fb->second};
    return std::make_optional<CacheEntry>(ce);
}

/** Receive a snoop from the mesh if there is one.
 */
void MeshInjector::receive_snoops()
{
    if (mesh_port.rx().snp[0]->can_take()) {
        auto snp = mesh_port.rx().snp[0]->take();
        ev_recv_snp(
            snp, snp->Opcode, snp->Addr, evd_srcid(snp->SrcID.id()), evd_tgtid(snp->TgtID.id()), evd_txnid(snp->TxnID));

        switch (snp->Opcode) {
        case chi::SnoopOpcode::SnpMakeInvalid: {
            cache.invalidate(snp, snp->Addr);
            l3c.invalidate(snp, snp->Addr);
            auto fb = pending_evictions.find(snp->Addr);
            if (fb != pending_evictions.end())
                fb->second = CacheEntry::State::I;
            make_response(snp, chi::ResponseOpcode::SnpResp);
            break;
        }
        case chi::SnoopOpcode::SnpCleanInvalid: {
            auto hit = cache.lookup(snp, snp->Addr, false);
            auto hit_l3 = l3c.lookup(snp, snp->Addr, false);
            auto fb = pending_evictions.find(snp->Addr);

            if (hit || hit_l3 || fb != pending_evictions.end()) {
                if ((hit && hit->is_dirty()) || (hit_l3 && hit_l3->is_dirty()))
                    make_data_response(snp, chi::DataOpcode::SnpRespData, chi::RespSnoop_I_PD);
                else
                    make_response(snp, chi::ResponseOpcode::SnpResp);

                if (hit)
                    cache.invalidate(snp, snp->Addr);
                else if (hit_l3)
                    l3c.invalidate(snp, snp->Addr);
                else if (fb != pending_evictions.end())
                    fb->second = CacheEntry::State::I;
            } else {
                make_response(snp, chi::ResponseOpcode::SnpResp);
            }
            break;
        }
        case chi::SnoopOpcode::SnpUniqueStash:
        case chi::SnoopOpcode::SnpStashShared: {
            auto snp_req = boost::make_local_shared<MemReqObj>(snp);
            snp_req->snp = snp;
            snp_req->stash_in_l3 = true;

            auto fb = fb_lookup(snp->Addr);

            if (fb) {
                make_response(snp, chi::ResponseOpcode::SnpResp);
                break;
            }

            auto maybe_txn_id = alloc_txn_id(select_bank(snp_req->snp->Addr), true);

            // accept stash only when there is enough stash reservation and no pending evictions
            if (maybe_txn_id.has_value()) {
                auto txn_id = *maybe_txn_id;

                waiting_requests.sleep({Block::Code::MEM_RESPONSE, txn_id}, snp_req);
                auto rsp = make_response(snp, chi::ResponseOpcode::SnpResp);
                rsp->DBID = txn_id;
                rsp->DatPull = DataPull::Read;

                if (dummy_evict)
                    create_dummy_evict_vaddr();
            } else {
                make_response(snp, chi::ResponseOpcode::SnpResp);
            }
            break;
        }
        case chi::SnoopOpcode::SnpCleanFwd:
        case chi::SnoopOpcode::SnpSharedFwd:
        case chi::SnoopOpcode::SnpUniqueFwd:
            snp_fwd_delay_queue.emplace(get_cycle() + l2_pipe_delay_in_cycles,
                                        std::make_tuple(std::bind_front(&MeshInjector::do_snp_fwd, this), snp));
            break;
        case chi::SnoopOpcode::SnpUnique:
            snp_unique_delay_queue.emplace(get_cycle() + l2_pipe_delay_in_cycles,
                                           std::make_tuple(std::bind_front(&MeshInjector::do_snp_unique, this), snp));
            break;
        case chi::SnoopOpcode::SnpOnce:
            snp_once_delay_queue.emplace(get_cycle() + l2_pipe_delay_in_cycles,
                                         std::make_tuple(std::bind_front(&MeshInjector::do_snp_once, this), snp));
            break;
        default:
            AMPERE_ERROR("unexpected snoop opcode {}", snp->Opcode);
            break;
        }
    }
}

void MeshInjector::receive_responses()
{
    receive_responses(mesh_port.rx().rsp[0]);

    if (mesh_port.nrsp == 2)
        receive_responses(mesh_port.rx().rsp[1]);
}

/** Receive a response from the mesh if there is one. Wake up the transaction that is
 *  waiting on the response.
 */
void MeshInjector::receive_responses(InWithCreditOpt<MeshRsp>& port)
{
    if (port->can_take()) {
        auto rsp = port->take();
        auto txn_id = rsp->TxnID;

        ev_recv_rsp(rsp,
                    rsp->Opcode,
                    evd_srcid(rsp->SrcID.id()),
                    evd_tgtid(rsp->TgtID.id()),
                    evd_txnid(rsp->TxnID),
                    rsp->CBusyS);

        switch (rsp->Opcode) {
        case chi::ResponseOpcode::RespSepData:
            waiting_requests.wake(
                {Block::Code::MEM_RESPONSE, txn_id},
                [rsp](MemReq req) -> bool {
                    req->wait_comp = false;
                    req->rsp = rsp;
                    return req->wait_comp == false && req->wait_data == false;
                },
                [this, rsp](MemReq req) {
                    refill_q.push_back(req);
                    make_response(req, chi::ResponseOpcode::CompAck, req->rsp->DBID);
                    AMPERE_ASSERT(outstanding_requests > 0, "outstanding_requests fell below 0");
                    --outstanding_requests;
                });
            cbusy->update(rsp->CBusyS, waiting_requests.sleep_count(Block::Code::MEM_RESPONSE));
            break;
        case chi::ResponseOpcode::RetryAck:
            waiting_requests.roll_over({Block::Code::MEM_RESPONSE, txn_id}, {Block::Code::PCREDIT, txn_id}, true);
            retry.push_retry(rsp->PCrdType, rsp->SrcID, txn_id);
            cbusy->update(rsp->CBusyS, waiting_requests.sleep_count(Block::Code::MEM_RESPONSE), true);
            AMPERE_ASSERT(outstanding_requests > 0, "outstanding_requests fell below 0");
            --outstanding_requests;
            qos_demote_untracked_txns.erase(txn_id);
            throttle_untracked_txns.erase(txn_id);
            break;
        case chi::ResponseOpcode::PCrdGrant: {
            auto retry_txn_id = retry.pop_retry(rsp->PCrdType, rsp->SrcID);
            waiting_requests.wake(
                {Block::Code::PCREDIT, retry_txn_id},
                [this, rsp](MemReq req) {
                    req->req->PCrdType = rsp->PCrdType;
                    req->req->AllowRetry = false;
                    if (throttle_retries) {
                        request_q.push_back(req);
                    } else {
                        priority_request_q.push_back(req);
                    }
                },
                true);
            cbusy->update(rsp->CBusyS, waiting_requests.sleep_count(Block::Code::MEM_RESPONSE));
            break;
        }
        case chi::ResponseOpcode::CompDBIDResp: {
            waiting_requests.wake({Block::Code::MEM_RESPONSE, txn_id}, [this, rsp](MemReq req) {
                req->rsp = rsp;
                writeout_q.push_back(req);
            });
            cbusy->update(rsp->CBusyS, waiting_requests.sleep_count(Block::Code::MEM_RESPONSE));
            AMPERE_ASSERT(outstanding_requests > 0, "outstanding_requests fell below 0");
            --outstanding_requests;
            break;
        }
        case chi::ResponseOpcode::Comp: {
            waiting_requests.wake({Block::Code::MEM_RESPONSE, txn_id}, [this, rsp](MemReq req) {
                if (req->req->ExpCompAck)
                    make_response(req, chi::ResponseOpcode::CompAck, rsp->DBID);

                if (req->req->Opcode == chi::RequestOpcode::CleanUnique
                    || req->req->Opcode == chi::RequestOpcode::MakeUnique)
                {
                    req->rsp = rsp;
                    refill_q.push_back(req);
                } else {
                    free_txn_id(rsp->TxnID);
                }
            });
            cbusy->update(rsp->CBusyS, waiting_requests.sleep_count(Block::Code::MEM_RESPONSE));
            AMPERE_ASSERT(outstanding_requests > 0, "outstanding_requests fell below 0");
            --outstanding_requests;
            break;
        }
        default:
            AMPERE_ERROR("unexpected response opcode {}", rsp->Opcode);
            break;
        }
    }
}

/** Receive data from the mesh if possible. The sleeping request is woken up and put on
 *  the refill queue to write the cache.
 */
void MeshInjector::receive_data()
{
    if (mesh_port.rx().dat[0]->can_take()) {
        auto dat = mesh_port.rx().dat[0]->take();
        auto txn_id = dat->TxnID;

        cbusy->update(dat->CBusyS, waiting_requests.sleep_count(Block::Code::MEM_RESPONSE));
        ev_recv_dat(dat,
                    dat->Opcode,
                    evd_srcid(dat->SrcID.id()),
                    evd_tgtid(dat->TgtID.id()),
                    evd_txnid(dat->TxnID),
                    dat->CBusyS,
                    evd_state(dat->Resp.encoding()));

        // Aside from event accounting, just ignore data chunks other than the first.
        if (dat->DataID != 0)
            return;

        AMPERE_UNCATEGORIZED_DEBUG("MeshInjector rcvdata: src={} tgt={} txn={} opcode={} DBID={}",
                                   dat->SrcID,
                                   dat->TgtID,
                                   dat->TxnID,
                                   dat->Opcode,
                                   dat->DBID);

        if (dat->Opcode == chi::DataOpcode::CompData) {
            waiting_requests.wake({Block::Code::MEM_RESPONSE, txn_id}, [this, dat](MemReq req) {
                req->wait_comp = false;
                req->wait_data = false;
                req->dat = dat;
                if (req->stash_in_l3) {
                    refill_l3_q.push_back(req);
                } else {
                    refill_q.push_back(req);
                    AMPERE_ASSERT(outstanding_requests > 0, "outstanding_requests fell below 0");
                    --outstanding_requests;
                }

                make_response(req, chi::ResponseOpcode::CompAck, dat->DBID);
            });
        } else if (dat->Opcode == chi::DataOpcode::DataSepResp) {
            waiting_requests.wake(
                {Block::Code::MEM_RESPONSE, txn_id},
                [dat](MemReq req) -> bool {
                    req->wait_data = false;
                    req->dat = dat;
                    return req->wait_comp == false && req->wait_data == false;
                },
                [this](MemReq req) {
                    refill_q.push_back(req);
                    make_response(req, chi::ResponseOpcode::CompAck, req->rsp->DBID);
                    AMPERE_ASSERT(outstanding_requests > 0, "outstanding_requests fell below 0");
                    --outstanding_requests;
                });
        } else if (dat->Opcode == chi::DataOpcode::CopyBackWrData) {
            waiting_requests.wake(
                {Block::Code::MEM_RESPONSE, txn_id},
                [dat](MemReq req) -> bool {
                    req->dat = dat;
                    return req->stash_in_l3 == true;
                },
                [this, dat](MemReq req) {
                    refill_l3_q.push_back(req);
                    make_response(req, chi::ResponseOpcode::CompAck, dat->DBID);
                });
        } else {
            AMPERE_ERROR("Unhandled data Opcode {}", dat->Opcode);
        }
    }
}

/** Send and waiting data out the data port.
 *
 */
void MeshInjector::send_data()
{
    if (!data_q.empty() && mesh_port.tx().dat[0]->can_put()) {
        auto dat = data_q.front();
        data_q.pop_front();
        mesh_port.tx().dat[0]->put(dat);
        ev_send_dat(dat,
                    dat->Opcode,
                    evd_srcid(dat->SrcID.id()),
                    evd_tgtid(dat->TgtID.id()),
                    evd_txnid(dat->TxnID),
                    evd_state(dat->Resp.encoding()));
    }
}

/** Determine if we've gone too many cycle without committing a uop. This probably means we're
 *  hung up waiting for a response that isn't coming.
 */
void MeshInjector::check_for_braindeath()
{
    if (get_cycle() - last_commit_cycle > brain_dead_limit)
        AMPERE_ERROR("{}: simulation brain dead", name());
}

/** Translate from the CHI response state to our local cache state.
 *
 *  FIXME: We should probably define a more complete cache state in chi/types.h itself.
 *  Currently the only reason we need a local definition is for UD state, which is not
 *  represented in a CHI response. Then we wouldn't need this function.
 *
 * @param state The CHI response state
 * @return The local cache state, which is identical for this subset of states.
 */
MeshInjector::CacheEntry::State MeshInjector::chi_state_to_cache_state(chi::FinalState state, bool make_dirty)
{
    switch (state) {
    case chi::FinalState::I:
        return CacheEntry::State::I;
    case chi::FinalState::SC:
        return CacheEntry::State::SC;
    case chi::FinalState::UC:
        return make_dirty ? CacheEntry::State::UD : CacheEntry::State::UC;
    case chi::FinalState::UD:
        return CacheEntry::State::UD;
    case chi::FinalState::SD:
        return CacheEntry::State::SD;
    default:
        AMPERE_FATAL("unknown FinalState");
        return CacheEntry::State::I;
    }
}

chi::FinalState MeshInjector::cache_state_to_chi_state(CacheEntry::State state)
{
    switch (state) {
    case CacheEntry::State::I:
        return FinalState::I;
    case CacheEntry::State::UC:
        return FinalState::UC;
    case CacheEntry::State::UD:
        return FinalState::UD;
    case CacheEntry::State::SC:
        return FinalState::SC;
    case CacheEntry::State::SD:
        return FinalState::SD;
    };

    AMPERE_FATAL("Unknown State");
    return FinalState::I;
}

void MeshInjector::stash_preload(PhysAddr address,
                                 chi::FinalState state,
                                 NodeIdToStasher node_id_to_stasher,
                                 chi::NodeId requester_node)
{
    auto victim = l3c.refill(nullptr, address, CacheEntry{address, chi_state_to_cache_state(state, false)}, true);

    if (victim) {
        auto stash_node = sam->lookup(victim->addr);
        auto stasher = node_id_to_stasher(stash_node);
        stasher->stash_invalidate(victim->addr, node_id);
    }
}

void MeshInjector::stash_invalidate(PhysAddr address, NodeId from_node)
{
    l3c.invalidate(nullptr, address, true);
}

Uop MeshInjector::create_load_uop(ProgramCounter pc, VirtAddr virt_addr)
{
    constexpr std::uint32_t ldr_b_32_opcode = 0b00'111'0'01'01'000000000000'00000'00000;
    std::shared_ptr<InstructionSimple> insn_ptr = std::make_shared<InstructionSimple>(pc, ldr_b_32_opcode, virt_addr);
    insn_ptr->set_decoder_uarch(UopUarch::FURIOSA);
    auto archx_instruction = insn_ptr;
    auto uop_set = archx_instruction->decode();
    AMPERE_ASSERT(!uop_set.empty(), "dummy load instruction did not decode");
    return make<Uop>(++dummy_uop_id, std::move(uop_set[0]), archx_instruction);
}

void MeshInjector::emit_dummy_evict()
{
    wait_next_dummy_evict--;

    if (wait_next_dummy_evict == 0) {
        if (!dummy_evict_vaddrs.empty()) {
            // Read request makes sure PHA/SF attains the right state to enable a legal dummy-evict.
            if (make_request(create_load_uop(0x1bc_pc, dummy_evict_vaddrs.front()),
                             read_with_read_not_shared_dirty ? chi::RequestOpcode::ReadNotSharedDirty
                                                             : chi::RequestOpcode::ReadShared))
            {
                ev_read_dummy();
            }

            dummy_evict_vaddrs.pop_front();
        }
        if (random_dummy_evict_delay)
            wait_next_dummy_evict = udist(rng);
        else
            wait_next_dummy_evict = dummy_evict_delay;
    }
}

void MeshInjector::create_dummy_evict_vaddr()
{
    last_evicted_vaddr += 64;
    dummy_evict_vaddrs.emplace_back(last_evicted_vaddr);
}

static std::ostream& operator<<(std::ostream& out, const Uop& uop)
{
    const char* opcode;

    if (uop->is_load())
        opcode = "Load";
    else if (uop->is_store())
        opcode = "Store";
    else
        opcode = "Misc";

    fmt::print(out, "{}: {}(", uop->id, opcode);

    if (uop->archx_uop->src1 != LReg::INVALID)
        fmt::print(out, "R{}", uop->archx_uop->src1);

    if (uop->archx_uop->src2 != LReg::INVALID) {
        if (uop->archx_uop->src1 == LReg::INVALID)
            fmt::print(out, "_, R{}", uop->archx_uop->src2);
        else
            fmt::print(out, ", R{}", uop->archx_uop->src2);
    }

    if (uop->archx_uop->src3 != LReg::INVALID) {
        if (uop->archx_uop->src2 == LReg::INVALID) {
            if (uop->archx_uop->src1 == LReg::INVALID)
                fmt::print(out, "_, _, R{}", uop->archx_uop->src3);
            else
                fmt::print(out, ", _, R{}", uop->archx_uop->src3);
        } else {
            fmt::print(out, ", R{}", uop->archx_uop->src3);
        }
    }

    fmt::print(out, ")");

    if (uop->is_load())
        fmt::print(out, "[{}]", uop->address());

    if (uop->archx_uop->dst != LReg::INVALID) {
        if (uop->archx_uop->dst2 != LReg::INVALID)
            fmt::print(out, " -> R{},R{}", uop->archx_uop->dst, uop->archx_uop->dst2);
        else
            fmt::print(out, " -> R{}", uop->archx_uop->dst);
    }

    if (uop->is_store())
        fmt::print(out, "[{}]", uop->address());

    if (uop->complete)
        fmt::print(out, " complete");

    return out;
}

static std::ostream& operator<<(std::ostream& out, const MemReq& memreq)
{
    if (memreq->req)
        fmt::print(out, "{} to {}", memreq->req->Opcode, memreq->req->Addr);
    else if (memreq->snp)
        fmt::print(out, "{} to {}", memreq->snp->Opcode, memreq->snp->Addr);
    return out;
}

template<typename T>
std::ostream& operator<<(std::ostream& out, const std::deque<T>& q)
{
    for (auto& entry : q)
        out << entry << '\n';
    return out;
}

template<typename T>
std::ostream& operator<<(std::ostream& out, const std::map<uint64_t, T>& q)
{
    for (auto& entry : q)
        out << entry.second << '\n';
    return out;
}

/** Dump internal state for debug.
 */
void MeshInjector::dump(std::ostream& out) const
{
    out << "txn_ids:" << m_txn_id_free_list[0].size() << "\n";
    out << "stash_txn_ids:" << m_stash_txn_id_free_list[0].size() << "\n";
    out << "sched_q:\n" << sched_q;
    out << "exec_q:\n" << exec_q;
    out << "commit_q:\n" << commit_q;
    out << "request_q:\n" << request_q;
    out << "refill_q:\n" << refill_q;
    out << "writeout_q:\n" << writeout_q;
    out << "data_q:\n" << data_q;
    out << "waiting_uops:\n" << waiting_uops;
    out << "waiting_requests:\n" << waiting_requests;
    out << name() << ": l2c_occupancy: " << const_cast<ampere::mesh::MeshInjector*>(this)->l2c_occupancy();
    out << "\n";
    out << name() << ": l3c_occupancy: " << const_cast<ampere::mesh::MeshInjector*>(this)->l3c_occupancy();
    out << "\n";
    cbusy->dump(out);
}

#ifdef USE_BOOST_SERIALIZATION
void MeshInjector::serialize_cache(boost::archive::text_oarchive& oa,
                                   const Cache<PhysAddr, CacheEntry>& cache_data) const
{
    oa << cache_data.get_num_sets();

    size_t entries{0};

    for (const auto& set : cache_data.get_sets())
        entries += set.size();

    oa << entries;

    size_t index{0};

    for (const auto& set : cache_data.get_sets()) {
        for (const auto& [tag, way_value] : set) {
            oa << index;
            oa << way_value.way;
            oa << tag;
            oa << way_value.value;
        }
        ++index;
    }
}

void MeshInjector::deserialize_cache(boost::archive::text_iarchive& ia, Cache<PhysAddr, CacheEntry>& cache_data)
{
    size_t num_sets;
    ia >> num_sets;

    AMPERE_ASSERT(num_sets == cache_data.get_num_sets(),
                  "sets in archive ({}) != sets in cache ({})\n",
                  num_sets,
                  cache_data.get_num_sets());

    for (auto& set : cache_data.get_sets())
        set.clear();

    size_t entries;
    ia >> entries;

    while (entries--) {
        size_t index;
        uint64_t way;
        uint64_t tag;
        CacheEntry value;

        ia >> index;
        ia >> way;
        ia >> tag;
        ia >> value;

        auto& set = cache_data.get_sets().at(index);
        set.insert({tag, {way, value}});
    }
}

void MeshInjector::serialize(boost::archive::text_oarchive& oa) const
{
    oa << std::string{name()};
    serialize_cache(oa, cache);
    serialize_cache(oa, l3c);
}

void MeshInjector::deserialize(boost::archive::text_iarchive& ia)
{
    deserialize_cache(ia, cache);
    deserialize_cache(ia, l3c);
}
#endif // USE_BOOST_SERIALIZATION

} // namespace ampere::mesh
