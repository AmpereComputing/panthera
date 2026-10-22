//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/archx/addr.h"
#include "ampere/core/prefetcher.h"
#include "ampere/archx/driver.h"
#include "ampere/archx/instruction.h"
#include "ampere/sc/param.h"
#include "ampere/sc/report.h"
#include "ampere/tigris/helpers.h"

#include <climits>
#include <cstdint>
#include <deque>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <type_traits>
#include <map>
#include <unordered_map>
#include <optional>
#include <algorithm>

namespace ampere::core {

template<typename T, typename PT = Prefetcher<T>>
class OraclePrefetcher : public PT
{
protected:
    using addr_t = decltype(PrefetcherData<T>::get_addr(std::declval<T>()));
    static constexpr bool is_code = std::is_same_v<addr_t, ProgramCounter>;
    static constexpr bool is_virtual = !std::is_same_v<addr_t, PhysAddr>;

    static constexpr bool KEEP_SOME_ACCESSES = false;
    static constexpr uint64_t ACCESSES_PER_BLOCK = 1024;

    static constexpr uint64_t MAX_POPULATE_RATIO = 4096;
    static constexpr uint64_t STALE_BLOCK_RATIO = 16;

    static constexpr uint64_t PAST_LOCATE_RATIO = 4;
    static constexpr uint64_t PAST_ACCESSES_LOCATE = ACCESSES_PER_BLOCK * PAST_LOCATE_RATIO;

    static constexpr uint64_t MAX_INSTRUCTIONS_RATIO = 4;
    static constexpr uint64_t MAX_INSTRUCTIONS_PER_BLOCK = ACCESSES_PER_BLOCK * MAX_INSTRUCTIONS_RATIO;

    static constexpr uint64_t NEXTADDR_LOOK_AHEAD_RATIO = 1;
    static constexpr uint64_t MAX_NEXTADDR_LOOK_AHEAD = ACCESSES_PER_BLOCK * NEXTADDR_LOOK_AHEAD_RATIO;

public:
    explicit OraclePrefetcher(sc_core::sc_module_name name) : PT(name) {}

    auto addr_of(T req) const { return PrefetcherData<T>::get_addr(req); }
    T make_prefetch_request(T req, addr_t addr, bool prefetch_into_requester = false);

    inline static bool has_hint(addr_t addr, uint64_t hint)
    {
        return hint && hint != std::numeric_limits<uint64_t>::max() && hint != static_cast<uint64_t>(addr);
    }
    virtual std::optional<int64_t> instruction_access_hint(const archx::Instruction &instruction) const { return {}; }
    uint64_t request_access_hint(T req) const;

    bool should_train(T req) const;
    bool is_code_request(T req) const;

    void train_impl(T req, bool hit, bool prefetch, bool demand) override
    {
        auto addr = addr_of(req).line_address();
        if (!hit)
            in_flight.try_emplace(addr, this->get_cycle());

        if (!should_train(req))
            return;

        auto &tracking = select_tracking(req);
        auto &current_access = tracking.current_access;
        auto &prefetch_offset = tracking.prefetch_offset;

        // accounts for out-of-order requests
        auto past_access = current_access > PAST_ACCESSES_LOCATE ? (current_access - PAST_ACCESSES_LOCATE) : 0;

        auto hint = request_access_hint(req);
        if (auto req_access = locate(tracking.accesses, past_access, current_access, addr, hint); req_access) {
            event.trigger(req, event.access_distance(*req_access - current_access));
            current_access = *req_access;

            if (this->prefetch_request->can_put()) {
                uint64_t prefetch_access = current_access + prefetch_offset;
                uint64_t max_prefetch_access =
                    current_access + static_cast<uint64_t>(prefetch_offset * max_prefetch_distance_ratio);
                if (auto prefetch =
                        next_address(tracking.accesses, current_access, prefetch_access, max_prefetch_access);
                    prefetch)
                {
                    auto [prefetch_acesss, prefetch_addr] = *prefetch;
                    T prefetch_req = make_prefetch_request(req, prefetch_addr, prefetch_into_requester);
                    this->prefetch_request->put(prefetch_req);
                    in_flight.emplace(prefetch_addr, this->get_cycle());
                    event.prefetch_attempt(req, event.prefetch_distance(prefetch_access - current_access));
                } else {
                    event.prefetch_skipped(req);
                }
            }
        } else {
            event.context_unknown(req);
        }

        cleanup(tracking.accesses, current_access);
    }

    void launch_impl(T req) override {}
    void overflow_impl(T req) override { in_flight.erase(addr_of(req).line_address()); }
    void drop_impl(T req, PrefetchDropReason reason) override
    {
        auto addr = addr_of(req).line_address();
        in_flight.erase(addr);
        cached.emplace(addr);
    }
    void fill_impl(T req, bool fb_pref, bool was_alloc_by_pf) override
    {
        auto addr = addr_of(req).line_address();
        cached.emplace(addr);

        auto was_in_flight = in_flight.find(addr);
        if (was_in_flight == in_flight.end())
            return;

        auto &tracking = select_tracking(req);
        auto &previous_offsets = tracking.previous_offsets;
        auto &prefetch_offset = tracking.prefetch_offset;

        if (previous_offsets.size() >= tracked_offsets)
            previous_offsets.pop_back();

        auto last_offset = this->get_cycle() - was_in_flight->second;
        previous_offsets.push_front(last_offset);

        prefetch_offset =
            std::accumulate(previous_offsets.begin(), previous_offsets.end(), 0ULL) / previous_offsets.size();
        in_flight.erase(was_in_flight);
    }
    void evict_impl(PhysAddr pa, bool prefetch, bool demand) override
    {
        cached.erase(addr_t(pa.line_address().value()));
    };

    void connect(Driver &driver) { this->driver.bind(driver); }
    void connect(DriverPort &driver) { this->driver.bind(driver); }

    inline static bool try_setup(PT *prefetcher, DriverPort &port)
    {
        if (prefetcher) {
            if (auto oracle = dynamic_cast<OraclePrefetcher<T, PT> *>(prefetcher); oracle) {
                oracle->connect(port);
                return true;
            }
        }
        return false;
    }

    template<typename P>
    inline static bool try_owned_setup(PT *prefetcher, P driver_func)
    {
        if (prefetcher) {
            if (auto oracle = dynamic_cast<OraclePrefetcher<T, PT> *>(prefetcher); oracle) {
                oracle->owned_driver = driver_func();
                oracle->connect(*oracle->owned_driver);
                return true;
            }
        }

        return false;
    }

protected:
    Param<bool> include_code{"include_code", "Prefetch code", is_code};
    Param<bool> include_data{"include_data", "Prefetch data", !is_code};
    Param<bool> include_stores{"include_stores", "Prefetch store data", false};
    Param<unsigned> initial_offset{"initial_offset", "Initial prefetch offset/distance", 1};
    Param<unsigned> tracked_offsets{"tracked_offsets", "Previous offsets (cycles from trigger to fill) to track", 16};
    Param<float> max_prefetch_distance_ratio{
        "max_prefetch_distance_ratio", "Max distance from current request to prefetch", 4};
    Param<bool> prefetch_into_requester{"prefetch_into_requester", "Prefetch into requester level", false};
    Param<bool> include_cacheline_splits{"include_cacheline_splits", "Include cacheline splits in prefetching", false};

    struct
    {
        Event trigger{"trigger", "Prefetch trigger/train"};
        Event prefetch_attempt{"prefetch_attempt", "Attempt prefetching"};
        Event prefetch_skipped{"prefetch_skipped", "Skipped prefetching likely because of distance"};
        Event context_unknown{"context_unknown", "Unable to locate context for prefetching"};

        EventData<int64_t> access_distance{"access_distance", "Distance in previous accesses"};
        EventData<int64_t> prefetch_distance{"prefetch_distance", "Distance access vs. prefetch"};
    } event;

    using Cached = std::unordered_set<addr_t>;
    Cached cached;

    using InFlight = std::unordered_map<addr_t, uint64_t>;
    InFlight in_flight;

    struct Accesses
    {
        uint64_t access_offset = 0;
        uint64_t prefetched = 0;

        std::vector<std::pair<addr_t, bool>> accesses;
        std::map<uint64_t, uint64_t> access_hint;
        mutable std::unordered_map<addr_t, std::deque<uint64_t>> address_accesses;

        explicit Accesses(uint64_t offset) : access_offset(offset) { accesses.reserve(ACCESSES_PER_BLOCK); }

        unsigned add(addr_t addr, std::optional<uint64_t> hint, const Cached &cache, const InFlight &in_flight)
        {
            auto line_addr = addr.line_address();
            bool already_present = cache.count(addr) || in_flight.count(addr);
            auto [addresses, added] = address_accesses.try_emplace(line_addr);
            addresses->second.emplace_back(accesses.size());
            access_hint.try_emplace(hint.value_or(accesses.size()), accesses.size());
            accesses.emplace_back(line_addr, already_present);
            if (already_present)
                ++prefetched;
            return (!added || already_present) ? 0 : 1;
        }

        unsigned add(const archx::Instruction &instruction,
                     unsigned index,
                     std::optional<uint64_t> hint,
                     const Cached &cache,
                     const InFlight &in_flight)
        {
            addr_t addr;
            if constexpr (is_virtual) {
                addr = instruction.virtual_address(index);
            } else {
                addr = instruction.physical_address(index);
            }
            auto size = tigris::memory_op_size(instruction, index);

            unsigned added = 0;
            added += add(addr, hint, cache, in_flight);

            auto end = addr + size - 1;
            if (!addr.same_cache_line(end)) {
                if constexpr (!is_virtual) {
                    if (!addr.same_page_4k(end)) {
                        auto tr = tigris::translate(instruction, instruction.virtual_address(index) + size - 1);
                        end = tr.result_address();
                    }
                }

                added += add(end, hint, cache, in_flight);
            }

            return added;
        }

        uint64_t last_access() const { return access_offset + accesses.size() - 1; }
        uint64_t last_hint() const { return access_hint.empty() ? 0 : access_hint.rbegin()->first; }

        bool more_to_prefetch() const { return !accesses.empty() && prefetched < accesses.size(); }
        bool maybe_within(uint64_t from, uint64_t to) const
        {
            if (accesses.empty() || address_accesses.empty())
                return false;
            return (from <= last_access() && to >= access_offset) || (to < access_offset);
        }

        uint64_t localize_access(uint64_t access) const
        {
            if (access > last_access())
                return accesses.size() - 1;
            if (access < access_offset)
                return 0;
            return access - access_offset;
        }

        std::optional<uint64_t> locate(uint64_t from_access, uint64_t to_access, addr_t addr, uint64_t hint) const
        {
            // FIXME: check for mismatch of addr/hint/access?
            if (has_hint(addr, hint)) {
                if (auto hint_it = access_hint.find(hint); hint_it != access_hint.end())
                    return hint_it->second + access_offset;
            }

            if (addr == addr_t{})
                return {};

            if (maybe_within(from_access, to_access)) {
                if (auto addr_it = address_accesses.find(addr); addr_it != address_accesses.end()) {
                    auto &addr_accesses = addr_it->second;
                    auto access = addr_accesses.front();
                    auto global_access = access + access_offset;
                    if (!KEEP_SOME_ACCESSES || addr_accesses.size() > 1 || global_access <= from_access) {
                        addr_accesses.pop_front();
                        if (addr_accesses.empty())
                            address_accesses.erase(addr_it);
                    }
                    if (global_access >= from_access)
                        return global_access;
                }
            }

            return {};
        }

        bool likely_unneeded(uint64_t access) const
        {
            return access > (last_access() + 1 + (ACCESSES_PER_BLOCK * STALE_BLOCK_RATIO));
        }

        std::optional<addr_t> try_prefetching(size_t access, const Cached &cache, const InFlight &in_flight)
        {
            if (auto &[addr, addr_prefetched] = accesses[access]; !addr_prefetched) {
                ++prefetched;
                addr_prefetched = true;
                if (!cache.count(addr) && !in_flight.count(addr))
                    return addr;
            }

            return {};
        }

        std::optional<std::pair<uint64_t, addr_t>> next_address(uint64_t from_access,
                                                                uint64_t to_access,
                                                                uint64_t max_access,
                                                                const Cached &cache,
                                                                const InFlight &in_flight)
        {
            if (more_to_prefetch()) {
                auto local_from_access = localize_access(from_access);
                auto local_to_access = localize_access(to_access);
                auto local_max_access = localize_access(max_access);

                // Try filling in from "to_access" to "from_access"
                for (int64_t i = local_to_access; i >= static_cast<int64_t>(local_from_access); --i)
                    if (auto addr = try_prefetching(static_cast<uint64_t>(i), cache, in_flight); addr)
                        return std::make_pair(i + access_offset, *addr);

                // If nothing, proceed with other future requests
                for (auto i = local_to_access + 1; i < accesses.size() && i <= local_max_access; ++i)
                    if (auto addr = try_prefetching(i, cache, in_flight); addr)
                        return std::make_pair(i + access_offset, *addr);
            }

            return {};
        }
    };

    uint64_t seen = 0;
    DriverPort driver;
    std::unique_ptr<Driver> owned_driver;

    using AccessList = std::deque<Accesses>;
    struct Tracking
    {
        uint64_t current_access = 0;
        uint64_t prefetch_offset = 0;
        std::deque<uint64_t> previous_offsets;
        AccessList accesses;

        explicit Tracking(uint64_t initial_prefetch_offset) : prefetch_offset(initial_prefetch_offset) {}
    };
    Tracking code_tracking{initial_offset};
    Tracking data_tracking{initial_offset};

    inline static bool should_record_access(const archx::Instruction &instruction, unsigned index)
    {
        return tigris::valid_memory_operation(instruction, index);
    }

    Tracking &select_tracking(T req)
    {
        if (is_code || is_code_request(req))
            return code_tracking;
        return data_tracking;
    }

    std::pair<uint64_t, Accesses *> create_or_reuse_block(AccessList &accesses)
    {
        auto offset = accesses.empty() ? 0 : accesses.back().last_access() + 1;
        auto *block = &((accesses.empty() || !accesses.back().accesses.empty()) ? accesses.emplace_back(offset)
                                                                                : accesses.back());
        return {offset, block};
    }

    uint64_t populate()
    {
        Accesses *code_block = nullptr;
        Accesses *data_block = nullptr;

        if (is_code || include_code)
            code_block = create_or_reuse_block(code_tracking.accesses).second;
        if (!is_code && include_data)
            data_block = create_or_reuse_block(data_tracking.accesses).second;

        AMPERE_ASSERT(code_block || data_block, "Data and code not enabled");

        uint64_t unique_added = 0;
        ProgramCounter next_pc;
        std::shared_ptr<archx::Instruction> instruction;

        size_t size = 0;
        for (uint64_t i = 0; size < ACCESSES_PER_BLOCK && i < MAX_INSTRUCTIONS_PER_BLOCK; ++i) {
            try {
                next_pc = driver->next_pc();
                instruction = driver->fetch(next_pc);
            } catch (const std::runtime_error &) {
                break;
            }
            auto hint = instruction_access_hint(*instruction);
            auto control = instruction->control();
            if (!control) {
                if constexpr (is_code) {
                    unique_added += code_block->add(instruction->program_counter(), hint, cached, in_flight);
                } else {
                    if (include_code) {
                        if constexpr (is_virtual) {
                            unique_added += code_block->add(
                                addr_t(instruction->program_counter().value()), hint, cached, in_flight);
                        } else {
                            unique_added += code_block->add(instruction->program_counter_pa(), hint, cached, in_flight);
                        }
                    }

                    if (include_data && (instruction->is_load() || (instruction->is_store() && include_stores))) {
                        for (unsigned i = 0, l = instruction->num_addresses(); i < l; ++i) {
                            if (should_record_access(*instruction, i)) {
                                addr_t access_addr;
                                if constexpr (is_virtual) {
                                    access_addr = instruction->virtual_address(i);
                                } else {
                                    access_addr = instruction->physical_address(i);
                                }
                                unique_added += include_cacheline_splits
                                                    ? data_block->add(*instruction, i, hint, cached, in_flight)
                                                    : data_block->add(access_addr, hint, cached, in_flight);
                            }
                        }
                    }
                }
            }

            driver->commit(instruction);

            if (control == archx::SimulationControl::STOP_MEASUREMENT)
                break;

            size = std::max<size_t>(code_block ? code_block->accesses.size() : 0,
                                    data_block ? data_block->accesses.size() : 0);
        }

        return unique_added;
    }

    // FIXME: x86 gcc 9.4 has some weird maybe-uninitialized on return {}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"

    template<uint64_t GIVE_UP_DISTANCE, typename P, typename... Args>
    auto do_or_populate(P func, AccessList &accesses, uint64_t from, uint64_t to, Args &&...args)
        -> decltype((std::declval<Accesses>().*func)(from, to, std::forward<Args>(args)...))
    {
        for (size_t i = 0, populated = 0; i <= accesses.size();) {
            bool last_empty = i == (accesses.size() - 1) && accesses.back().accesses.empty();
            if (i == accesses.size() || last_empty) {
                auto added = populate();
                if (!added && accesses.back().accesses.empty())
                    return {};

                populated += added;
                if (populated >= (MAX_POPULATE_RATIO * ACCESSES_PER_BLOCK))
                    throw std::out_of_range("Populated too much, likely dead");
                else if (populated >= (ACCESSES_PER_BLOCK * STALE_BLOCK_RATIO))
                    i -= cleanup(accesses, accesses.back().last_access());
            }

            if constexpr (GIVE_UP_DISTANCE < ULLONG_MAX) {
                if ((to + GIVE_UP_DISTANCE) < accesses[i].access_offset)
                    return {};
            }

            if (auto result = (accesses[i].*func)(from, to, std::forward<Args>(args)...); result)
                return *result;

            if (!last_empty)
                ++i;
        }

        return {};
    }

#pragma GCC diagnostic pop

    auto locate(AccessList &accesses, uint64_t from_access, uint64_t to_access, addr_t addr, uint64_t hint)
    {
        try {
            if (has_hint(addr, hint)) {
                while (accesses.empty() || accesses.back().last_hint() < hint) {
                    auto added = populate();
                    if (!added && (accesses.empty() || accesses.back().accesses.empty()))
                        break;
                    if (!accesses.empty())
                        cleanup(accesses, accesses.back().last_access());
                }
            }
            return do_or_populate<ULLONG_MAX>(&Accesses::locate, accesses, from_access, to_access, addr, hint);
        } catch (const std::out_of_range &) {
            auto access = accesses.front().access_offset;
            for (const auto &block : accesses) {
                for (const auto &[access_addr, prefetched] : block.accesses) {
                    if (addr == access_addr) {
                        std::cerr << fmt::format("Info: Found at access {} (block offset {}), prefetched: {}",
                                                 access,
                                                 block.access_offset,
                                                 prefetched);

                        if (auto addr_it = block.address_accesses.find(addr); addr_it != block.address_accesses.end()) {
                            std::cerr << ", in addr map with " << (addr_it->second.front() + block.access_offset);
                        } else {
                            std::cerr << ", not in addr map";
                        }
                        std::cerr << std::endl;
                    }
                    ++access;
                }
            }
            throw std::runtime_error(
                fmt::format("Dead on locating address {:#018x} (hint {}), from {} to {}, populated until {}",
                            addr.value(),
                            hint,
                            from_access,
                            to_access,
                            accesses.empty() ? 0 : accesses.back().access_offset));
        }
    }

    auto next_address(AccessList &accesses, uint64_t from_access, uint64_t to_access, uint64_t max_access)
    {
        try {
            return do_or_populate<MAX_NEXTADDR_LOOK_AHEAD>(
                &Accesses::next_address, accesses, from_access, to_access, max_access, cached, in_flight);
        } catch (const std::out_of_range &) {
            throw std::runtime_error(fmt::format("Dead on next address, from {} to {}, populated until {}",
                                                 from_access,
                                                 to_access,
                                                 accesses.empty() ? 0 : accesses.back().access_offset));
        }
    }

    unsigned cleanup(AccessList &accesses, uint64_t access)
    {
        unsigned removed = 0;
        for (auto it = accesses.begin(); it != accesses.end();) {
            if (!it->likely_unneeded(access))
                break;

            ++removed;
            it = accesses.erase(it);
        }

        return removed;
    }
};

template<>
Fetch OraclePrefetcher<Fetch>::make_prefetch_request(Fetch req, ProgramCounter addr, bool prefetch_into_requester);

template<>
uint64_t OraclePrefetcher<Fetch>::request_access_hint(Fetch req) const;

template<>
bool OraclePrefetcher<Fetch>::should_train(Fetch req) const;

template<>
bool OraclePrefetcher<Fetch>::is_code_request(Fetch req) const;

template<>
MemReq OraclePrefetcher<MemReq>::make_prefetch_request(MemReq req, PhysAddr addr, bool prefetch_into_requester);

template<>
uint64_t OraclePrefetcher<MemReq>::request_access_hint(MemReq req) const;

template<>
bool OraclePrefetcher<MemReq>::should_train(MemReq req) const;

template<>
bool OraclePrefetcher<MemReq>::is_code_request(MemReq req) const;

class CP_Oracle : public OraclePrefetcher<Fetch>, public implementation<CP_Oracle, Prefetcher<Fetch>>
{
public:
    explicit CP_Oracle(sc_core::sc_module_name name);
};

class L2P_Oracle : public OraclePrefetcher<MemReq>, public implementation<L2P_Oracle, Prefetcher<MemReq>>
{
    Param<bool> use_instruction_id_as_hint{
        "use_instruction_id_as_hint", "Use instruction nyumber as hint for locating memory access", true};

public:
    explicit L2P_Oracle(sc_core::sc_module_name name);

    std::optional<int64_t> instruction_access_hint(const archx::Instruction &instruction) const override
    {
        if (use_instruction_id_as_hint)
            return instruction.inum().id();
        return {};
    }
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
