//
// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <algorithm>
#include <vector>

#include "ampere/archx/uop_info.h"
#include "ampere/core/idr/idr_defs.h"
#include "ampere/core/ixu_defs.h"
#include "ampere/sc/credit.h"
#include "ampere/sc/module.h"
#include "ampere/sc/param.h"
#include "ampere/sc/poly.h"
#include "ampere/sc/sc.h"
#include "ampere/sc/submodule.h"
#include "ampere/core/uop.h"

namespace ampere::core {

using sc::CreditPool;
using sc::Module;
using sc::Param;
using sc::poly;
using sc::Submodule;

class IDRPartition;

struct AssignmentProperties
{
    unsigned width = 0;
    const SchedulerCredits& sch;
    bool uses_pre_swizzle_partition = false;
    unsigned partitions = 1;

    unsigned partition_of(const ConstUop& uop) const;
    unsigned lane_of(const ConstUop& uop) const;
};

class IDRSchedulerAssignment : public Module
{
protected:
    AssignmentProperties properties;

    unsigned partition_of(const ConstUop& uop) const { return properties.partition_of(uop); }
    unsigned lane_of(const ConstUop& uop) const { return properties.lane_of(uop); }

public:
    using assignment_t = std::optional<Scheduler>;

    IDRSchedulerAssignment(const sc_core::sc_module_name& name, const AssignmentProperties& properties)
        : Module(name), properties(properties)
    {
    }

    // Reset assignment policy state
    virtual void reset() {}
    // Assignment was started (for snapshoting assignment policy state)
    virtual void start_assignment() {}
    // Assignment was stalled (for restore of asssignment policy state)
    virtual void stalled() {}
    // Assign a scheduler for all uops within the group. By default just calls
    // assign_uop on each, but can be overridden to handle multiple/cross-uop
    // assignment policies/rules. If no assignment for a uop is provided, one
    // will be provided by the caller (e.g. least loaded).
    virtual std::vector<assignment_t> assign_uops(const std::vector<Uop>& uops)
    {
        std::vector<assignment_t> assigned;
        assigned.reserve(uops.size());

        for (const auto& uop : uops)
            assigned.emplace_back(assign_uop(uop));

        return assigned;
    }

protected:
    // Assign scheduler for a single uop, not taking into account any other uops
    // that may be in the group
    virtual assignment_t assign_uop(const Uop& uop) { return {}; }
};

using IDRSchedulerAssignmentPolyDef =
    sc::poly_definition<IDRSchedulerAssignment, sc::poly_constructor<const AssignmentProperties&>>;

// Assignment policy for layering other policies
class MultipleIDRSchedulerAssignment : public IDRSchedulerAssignment,
                                       IDRSchedulerAssignmentPolyDef::implementation<MultipleIDRSchedulerAssignment>
{
    std::list<Submodule<IDRSchedulerAssignment>> assignments;

    void add_assignment(std::string name, const json& params)
    {
        auto& parameters = this->get_environment()->parameters;
        if (params.is_object()) {
            for (const auto& [key, value] : params.items()) {
                if (key != "name")
                    parameters.add_setting(fmt::format("{}.{}.{}", this->name(), name, key), value, "param");
            }
        } else {
            parameters.add_setting(fmt::format("{}.{}.type", this->name(), name), params, "param");
        }

        assignments.emplace_back(name.c_str(), sc::as_const_ref(properties));
    }

public:
    MultipleIDRSchedulerAssignment(const sc_core::sc_module_name& name, const AssignmentProperties& properties)
        : IDRSchedulerAssignment(name, properties)
    {
        auto& parameters = this->get_environment()->parameters;
        if (auto* settings = parameters.match_setting(fmt::format("{}.assignments", this->name())); settings) {
            AMPERE_ASSERT(
                settings->value().is_array(), "Expected array of assignments, got {}", settings->value().type_name());

            for (const auto& params : settings->value()) {
                AMPERE_ASSERT(params.is_object(), "Expected dict in array, got {}", params.type_name());
                if (!params["type"].is_null())
                    add_assignment(params["name"], params);
            }
        }
    }

    void stalled() override;
    void start_assignment() override;
    std::vector<assignment_t> assign_uops(const std::vector<Uop>& uops) override;
};

class LeastLoadedScheduler
{
    AssignmentProperties properties;
    bool update_within_cycle{false};
    std::optional<SchedulerType> default_scheduler;

    std::vector<unsigned> sch_assigned_this_cycle;

public:
    explicit LeastLoadedScheduler(const AssignmentProperties& properties,
                                  bool update_within_cycle = false,
                                  std::optional<SchedulerType> default_scheduler = {})
        : properties(properties),
          update_within_cycle(update_within_cycle),
          default_scheduler(default_scheduler),
          sch_assigned_this_cycle(properties.sch.size(), 0)
    {
    }

    void reset() { std::fill(sch_assigned_this_cycle.begin(), sch_assigned_this_cycle.end(), 0); }
    Scheduler assign(const Uop& uop);
};

// Assignment policy which just provides the least loaded scheduler
class LeastLoadedIDRSchedulerAssignment
    : public IDRSchedulerAssignment,
      IDRSchedulerAssignmentPolyDef::implementation<LeastLoadedIDRSchedulerAssignment>
{
    Param<bool> smart_least_loaded{"smart_least_loaded",
                                   "Take into account schedulers already assigned this cycle when picking the "
                                   "least-loaded scheduler. Intended for use when pipe_balancing is disabled.",
                                   false};
    Param<std::optional<SchedulerType>> simple_default_scheduler{
        "simple_default_scheduler",
        "Use simpler and more consistent pipe assignment when the choice of pipe doesn't matter",
        {}};

    LeastLoadedScheduler least_loaded;

public:
    LeastLoadedIDRSchedulerAssignment(const sc_core::sc_module_name& name, const AssignmentProperties& properties)
        : IDRSchedulerAssignment(name, properties),
          least_loaded(properties, smart_least_loaded, *simple_default_scheduler)
    {
    }

    void reset() override { least_loaded.reset(); }

protected:
    assignment_t assign_uop(const Uop& uop) override { return least_loaded.assign(uop); }
};

// More generic scheduler balancing policy
class BalancingIDRSchedulerAssignment : public IDRSchedulerAssignment,
                                        IDRSchedulerAssignmentPolyDef::implementation<BalancingIDRSchedulerAssignment>
{
    Param<unsigned> balancing_threshold{
        "balancing_threshold", "Threshold of scheduler difference for when to force to more empty scheduler", 4};
    Param<bool> avoid_full{
        "avoid_full", "Don't assign a uop to a full scheduler when the other scheduler has space", false};

public:
    BalancingIDRSchedulerAssignment(const sc_core::sc_module_name& name, const AssignmentProperties& properties)
        : IDRSchedulerAssignment(name, properties)
    {
    }

    std::vector<assignment_t> assign_uops(const std::vector<Uop>& uops) override;
};

class ThisOrThatAssignment : public IDRSchedulerAssignment
{
protected:
    Param<SchedulerType> first_scheduler_type{"first_scheduler", "First scheduler choice", "A"};
    Param<SchedulerType> second_scheduler_type{"second_scheduler", "Second scheduler choice", "B"};

public:
    ThisOrThatAssignment(const sc_core::sc_module_name& name, const AssignmentProperties& properties)
        : IDRSchedulerAssignment(name, properties)
    {
    }
};

class BinaryAssignment : public ThisOrThatAssignment
{
protected:
    Param<unsigned> first_balancing_threshold;
    Param<unsigned> second_balancing_threshold;
    Param<bool> avoid_full{
        "avoid_full", "Don't assign a uop to a full scheduler when the other scheduler has space", false};
    Param<bool> use_partition_for_default{
        "use_partition_for_default", "Use partition for default instead of lane", false};

    Scheduler first_scheduler;
    Scheduler second_scheduler;

    unsigned first_available{0};
    unsigned second_available{0};

public:
    BinaryAssignment(const sc_core::sc_module_name& name, const AssignmentProperties& properties)
        : ThisOrThatAssignment(name, properties),
          first_balancing_threshold("first_balancing_threshold",
                                    "If sched first - sched second credits are more than this, force uops to first",
                                    properties.width),
          second_balancing_threshold("second_balancing_threshold",
                                     "If sched second - sched first credits are more than this, force uops to second",
                                     properties.width)
    {
    }

protected:
    assignment_t assign_uop(const Uop& uop) override;
    std::vector<assignment_t> assign_uops(const std::vector<Uop>& uops) override;

    void update_credits_for_partition(unsigned partition);
    bool first_has_more_credits() const;
    bool second_has_more_credits() const;

    // Assignment for a single uop
    virtual assignment_t single_uop_assignment(const Uop& uop) { return std::nullopt; }
    // Fallback assignment used if assign_uop does not provide anything
    virtual assignment_t fallback_uop_assignment(unsigned index, const Uop& uop, const assignment_t& neighbor);
};

// Binary (first or second) assignemnt policy with some optional balancing between
class BinaryIDRSchedulerAssignment : public BinaryAssignment,
                                     IDRSchedulerAssignmentPolyDef::implementation<BinaryIDRSchedulerAssignment>
{
public:
    BinaryIDRSchedulerAssignment(const sc_core::sc_module_name& name, const AssignmentProperties& properties)
        : BinaryAssignment(name, properties)
    {
    }
};

class PingPong
{
protected:
    std::array<bool, MAX_PARTITIONS> next_scheduler_by_partition{};
    std::array<bool, MAX_PARTITIONS> prev_scheduler_by_partition{};

public:
    PingPong() = default;

    void snapshot() { prev_scheduler_by_partition = next_scheduler_by_partition; }
    void restore() { next_scheduler_by_partition = prev_scheduler_by_partition; }

    SchedulerType next_scheduler(SchedulerType first_scheduler_type,
                                 SchedulerType second_scheduler_type,
                                 unsigned partition)
    {
        partition = Scheduler::normalized(first_scheduler_type, partition).partition;
        auto& next = next_scheduler_by_partition[partition];
        auto sched = next ? second_scheduler_type : first_scheduler_type;
        next = !next;
        return sched;
    }
};

//
// Binary (first or second) assignment policy with optional ping-pong assignment
// for specified pipe
class BinaryPingPongIDRSchedulerAssignment
    : public BinaryAssignment,
      IDRSchedulerAssignmentPolyDef::implementation<BinaryPingPongIDRSchedulerAssignment>
{
    Param<std::optional<Uoppipe>> ping_pong_pipe{"ping_pong_pipe", "Pipe which uses ping-pong", {}};
    PingPong ping_pong;

public:
    BinaryPingPongIDRSchedulerAssignment(const sc_core::sc_module_name& name, const AssignmentProperties& properties)
        : BinaryAssignment(name, properties)
    {
    }

    void stalled() override { ping_pong.restore(); }

protected:
    void start_assignment() override { ping_pong.snapshot(); }

    bool use_ping_pong(const Uop& uop) const { return ping_pong_pipe && uop->allowed_pipes() == *ping_pong_pipe; }
    assignment_t single_uop_assignment(const Uop& uop) override;
    assignment_t fallback_uop_assignment(unsigned index, const Uop& uop, const assignment_t& neighbor) override;
};

// Ping-pong only assignment for specific pipe
class PingPongAssignment : public ThisOrThatAssignment,
                           IDRSchedulerAssignmentPolyDef::implementation<PingPongAssignment>
{
    Param<Uoppipe> pipe{"pipe", "Pipe", "Z"};
    PingPong ping_pong;

public:
    PingPongAssignment(const sc_core::sc_module_name& name, const AssignmentProperties& properties)
        : ThisOrThatAssignment(name, properties)
    {
    }

protected:
    assignment_t assign_uop(const Uop& uop) override;
};

// Ping-pong assignemnt for specific FSU unit
class FSUUnitPingPongAssignment : public ThisOrThatAssignment,
                                  IDRSchedulerAssignmentPolyDef::implementation<FSUUnitPingPongAssignment>
{
    Param<FsuUnit> unit{"unit", "FSU Unit to ping-pong assignment", {}};
    PingPong ping_pong;

public:
    FSUUnitPingPongAssignment(const sc_core::sc_module_name& name, const AssignmentProperties& properties)
        : ThisOrThatAssignment(name, properties)
    {
    }

protected:
    assignment_t assign_uop(const Uop& uop) override;
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
