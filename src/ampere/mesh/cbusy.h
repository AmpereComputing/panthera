//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/mesh/chi/opcodes.enum.h"
#include "ampere/mesh/cbusy_base.h"
#include "ampere/sc/counter.h"
#include "ampere/sc/sc.h"

namespace ampere::mesh {

using namespace sc;

class CBusy : public CBusyBase, poly<CBusy>
{
public:
    CBusy(const sc_core::sc_module_name& name);

    Param<bool> enable{"enable", "Enable CBusy adjustments", true};
    Param<unsigned> window{
        "window", "Number of transactions that are considered before enacting a throttle event", 256};
    Param<unsigned> threshold_down{
        "threshold_down", "Value threshold must cross to trigger a throttle down event", 124};
    Param<unsigned> threshold_up{"threshold_up", "Value threshold must cross to trigger a throttle up event", 124};
    Param<unsigned> step_down{
        "step_down", "Number of steps to decrease outstanding requests when athrottle down event occurs", 8};
    Param<unsigned> step_up{
        "step_up", "Number of steps to increase outstanding request when a throttled up event occurs", 4};
    Param<unsigned> throttle_min{"throttle_min", "Never decrease number of outstanding requests by this number", 4};
    Param<unsigned> throttle_max{"throttle_max", "Never increase number of outstanding request by this number", 48};
    Param<unsigned> throttle_start{
        "throttle_start", "Starting value for outstanding requests (default is throttle_max)", throttle_max.val()};
    Param<bool> throttle_prefetchers{"throttle_prefetchers", "Enable throttling of prefetchers", true};

    /* Completer Params */
    Param<unsigned> c_threshold_high{
        "c_threshold_high",
        "Number of requests outstanding in completer for completer to be considered busy high.",
        10};
    Param<unsigned> c_threshold_mid{
        "c_threshold_mid", "Number of requests outstanding in completer for completer to be considered busy mid.", 5};
    Param<unsigned> c_threshold_low{
        "c_threshold_low", "Number of requests outstanding in completer for completer to be considered busy low.", 0};

    Param<chi::CBusyState> c_high_state{
        "c_high_state", "CBusy state value returned when completer in high utilization", chi::CBusyState::High};
    Param<chi::CBusyState> c_mid_state{
        "c_mid_state", "CBusy state value returned when completer in medium utilization", chi::CBusyState::Medium};
    Param<chi::CBusyState> c_low_state{
        "c_low_state", "CBusy state value returned when completer in low utilization", chi::CBusyState::Low};

    Event ev_throttle{"throttle", "A cbusy throttle event occured"};
    Event ev_reset{"reset", "The cbusy window was reset"};
    EventData<unsigned> evd_max_outstanding_requests{
        "max_outstanding_requests",
        "Current maximum number of outstanding requests as adjusted by cbusy throttle events"};
    EventData<unsigned> evd_outstanding_requests{"outstanding_requests",
                                                 "Current number of outstanding requests as supplied by the caller"};
    EventData<bool> evd_prefetcher_enabled{"prefetcher_enabled", "Can prefetches be used for outstanding requests"};
    EventData<unsigned> evd_high{"high", "Number of high cbusy responses received"};
    EventData<unsigned> evd_low{"low", "Number of low cbusy responses received"};

    void dump(std::ostream& os) const override;

    /**********************
     * Completer Accesors *
     *********************/
    chi::CBusyState update_cbusy(unsigned outstanding_reqs,
                                 chi::CBusyState rbusy = chi::CBusyState::None,
                                 bool retry_ack = false) const override;

    /**********************
     * Requester Accesors *
     *********************/
    // Shall be called requester for every chi response and data packet.  Updates internal FSM
    void update(chi::CBusyState cbusy, int outstanding_reqs = -1, bool retry = false) override;

    // Shall be called by requester to determine how many outstanding requests is being allowed
    unsigned num_requests() const override { return c_outstanding.get_value(); }

    // Shall be called by requester to determine if prefetch requests are allowed
    bool prefetcher_throttled() { return throttle_prefetchers ? !c_prefetcher : false; }

protected:
    /* Requester fields*/
    unsigned low_busy{0};     // Accumulator of low busy notifications from completers
    unsigned high_busy{0};    // Accumulator of high busy notifications from completers
    unsigned window_count{0}; // Number of transactions since last window reset
    SaturatingCounter c_outstanding{
        throttle_min, throttle_max, throttle_start}; // Current number of allowed oustanding requests
    bool c_prefetcher{true};                         // Can prefetches be sent via outstanding requests

    void reset_window(int outstanding_reqs);
};

} // namespace ampere::mesh
