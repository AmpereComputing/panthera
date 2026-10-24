//
// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <set>

#include "ampere/core/idr/idr_defs.h"
#include "ampere/core/ixu_defs.h"
#include "ampere/sc/credit.h"
#include "ampere/sc/module.h"
#include "ampere/sc/param.h"
#include "ampere/sc/poly.h"
#include "ampere/sc/sc.h"
#include "ampere/sc/submodule.h"

namespace ampere::core {

using sc::CreditPool;
using sc::Module;
using sc::Param;
using sc::poly;
using sc::Submodule;

class IDRPartition;

struct ThrottlingProperties
{
    unsigned width;
    const SchedulerCredits& sch;
};

class IDRSchedulerThrottle : public Module
{
protected:
    ThrottlingProperties properties;

public:
    IDRSchedulerThrottle(const sc_core::sc_module_name& name, const ThrottlingProperties& properties)
        : Module(name), properties(properties)
    {
    }

    // Is scheduler on partition currently throttled
    virtual bool throttled(SchedulerType sched, unsigned partition) const = 0;
    // Adjust (add or remove) eligible schedulers for partition
    virtual void adjust(std::set<SchedulerType>& eligible, unsigned partition) = 0;
};

using IDRSchedulerThrottlePolyDef =
    sc::poly_definition<IDRSchedulerThrottle, sc::poly_constructor<const ThrottlingProperties&>>;

class IDRSchedulerThrottleNone : public IDRSchedulerThrottle,
                                 IDRSchedulerThrottlePolyDef::implementation<IDRSchedulerThrottleNone>
{
public:
    IDRSchedulerThrottleNone(const sc_core::sc_module_name& name, const ThrottlingProperties& properties)
        : IDRSchedulerThrottle(name, properties)
    {
    }
    bool throttled(SchedulerType sched, unsigned partition) const override { return false; }
    void adjust(std::set<SchedulerType>& eligible, unsigned partition) override {}
};

// Always exclude/replace scheduler throttling
class IDRSchedulerThrottleAlways : public IDRSchedulerThrottle,
                                   IDRSchedulerThrottlePolyDef::implementation<IDRSchedulerThrottleAlways>
{
    Param<SchedulerType> exclude_scheduler{"exclude", "Exclude scheduler", {}};
    Param<std::optional<SchedulerType>> replace_scheduler{"replace", "Replace with scheduler", {}};

public:
    IDRSchedulerThrottleAlways(const sc_core::sc_module_name& name, const ThrottlingProperties& properties)
        : IDRSchedulerThrottle(name, properties)
    {
    }

    bool throttled(SchedulerType sched, unsigned partition) const override { return sched == exclude_scheduler; }
    void adjust(std::set<SchedulerType>& eligible, unsigned partition) override
    {
        if (eligible.erase(exclude_scheduler) && replace_scheduler)
            eligible.emplace(*replace_scheduler);
    }
};

// Scheduler entry watermark-based throttling
class IDRSchedulerThrottleWatermark : public IDRSchedulerThrottle,
                                      IDRSchedulerThrottlePolyDef::implementation<IDRSchedulerThrottleWatermark>
{
    Param<SchedulerType> watermark_scheduler{"watermark_scheduler", "Scheduler to use for watermark checks", "X"};
    Param<SchedulerType> throttle_scheduler{"throttle_scheduler", "Scheduler to throttle (exclude)", "Y"};
    Param<bool> replace_with_watermark_scheduler{
        "replace_with_watermark_scheduler", "Replace throttled scheduler with scheduler used for watermark", true};

    Param<unsigned> unthrottle_watermark{
        "unthrottle_watermark", "Watermark of how many scheduler entries to stop throttling (less than)", 0};
    Param<unsigned> throttle_watermark{
        "throttle_watermark", "Watermark of how many scheduler entries to start throttling (greater than)", 0};

    mutable std::array<bool, MAX_PARTITIONS> throttling{};

    bool is_throttling(unsigned partition) const;

public:
    explicit IDRSchedulerThrottleWatermark(const sc_core::sc_module_name& name, const ThrottlingProperties& properties)
        : IDRSchedulerThrottle(name, properties)
    {
    }

    bool throttled(SchedulerType sched, unsigned partition) const override
    {
        return sched == throttle_scheduler && is_throttling(partition);
    }
    void adjust(std::set<SchedulerType>& eligible, unsigned partition) override;
};

class IDRWidthThrottle : public Module
{
protected:
    ThrottlingProperties properties;

public:
    IDRWidthThrottle(const sc_core::sc_module_name& name, const ThrottlingProperties& properties)
        : Module(name), properties(properties)
    {
    }

    // Is currently throttled (narrower width)
    virtual bool throttled() const = 0;
    // Current decode width
    virtual unsigned width() const = 0;
};

using IDRWidthThrottlePolyDef =
    sc::poly_definition<IDRWidthThrottle, sc::poly_constructor<const ThrottlingProperties&>>;

class IDRWidthThrottleNone : public IDRWidthThrottle, IDRWidthThrottlePolyDef::implementation<IDRWidthThrottleNone>
{
public:
    IDRWidthThrottleNone(const sc_core::sc_module_name& name, const ThrottlingProperties& properties)
        : IDRWidthThrottle(name, properties)
    {
    }
    bool throttled() const override { return false; }
    unsigned width() const override { return properties.width; }
};

// Decode width throttling based upon IXU/FSU throughput signals
class IDRWidthThrottleIXUFSUThroughput : public IDRWidthThrottle,
                                         IDRWidthThrottlePolyDef::implementation<IDRWidthThrottleIXUFSUThroughput>
{
    Param<unsigned> throttle_width{"throttle_width", "Decode width when throttled", 2};

    bool low_issue_rate_detected[2]{false}; // rate[0] is current and rate[1] is previous

public:
    sc::In<bool> ixu_throttle{"ixu_throttle"};
    sc::In<bool> fsu_throttle{"fsu_throttle"};

    IDRWidthThrottleIXUFSUThroughput(const sc_core::sc_module_name& name, const ThrottlingProperties& properties)
        : IDRWidthThrottle(name, properties)
    {
    }

    bool run() override;

    bool throttled() const override { return low_issue_rate_detected[0] && low_issue_rate_detected[1]; }
    unsigned width() const override { return throttled() ? throttle_width : properties.width; }
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
