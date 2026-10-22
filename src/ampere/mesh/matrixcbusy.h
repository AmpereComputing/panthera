//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/mesh/chi/opcodes.enum.h"
#include "ampere/sc/sc.h"
#include "ampere/mesh/cbusy_base.h"

namespace ampere::mesh {

using namespace sc;

class MatrixCBusy : public CBusyBase, poly<MatrixCBusy>
{
public:
    MatrixCBusy(const sc_core::sc_module_name &name);

    Param<bool> enable{"enable", "Enable CBusy adjustments", false};

    /* completer parameters */
    Param<bool> use_highest_cbusy{"use_highest_cbusy", "Use the higher cbusy value between cbusy and dscbusy", false};
    Param<uint8_t> num_cntr{"num_cntr", "Number of ReqRcvd and ReqDealloc counters", 4};
    Param<int> window{"window", "number of cycles over which counter values are accummulated", 2048};
    Param<int> phase_cycles{"phase_cycles", "number of cycles used a offset between counters", 512};
    Param<float> rho_max{"rho_max", "Max value rho is allowed to take in normal CBusy throttling", 15.0};
    Param<size_t> threshold{"threshold", "threshold value below which cbusy is not enabled", 10};
    Param<uint8_t> highest_cbusy{"highest_cbusy", "highest non MPAM value of cbusy", 7};
    Param<uint8_t> lowest_cbusy{"lowest_cbusy", "lowest value of cbusy possible", 0};
    Param<std::vector<float>> completer_lut{"completer_lut", "CBusy completer range configuration", "[]"_json};
    Param<std::vector<size_t>> occupancy_busyness{
        "occupancy_busyness",
        "also factor in occupancy to CBusy calculations; each entry is a threshold for a CBusy level",
        "[]"_json};
    Param<std::vector<uint8_t>> occupancy_cbusy_value{
        "occupancy_cbusy_value", "table of values to apply at each threshold in \"occupancy_cbusyness\"", "[]"_json};

    /* Requester parameters */
    Param<std::vector<int>> requester_lut{"requester_lut", "RCBusy LUT", "[]"_json};
    Param<std::vector<int>> dscbusy{"dscbusy", "RCBusy DSCBusy LUT", "[]"_json};
    Param<std::vector<int>> throttle{"throttle", "RCBusy throttle LUT", "[]"_json};
    Param<std::vector<int>> qos_threshold{
        "qos_threshold",
        "Outstanding request threshold at which to demote reads. "
        "Expressed as an array of: 0, 1, 2, or 3, with 0=disabled, 1=1/4 max, 2=1/2 max, 3=3/4 max",
        "[]"_json};
    Param<int16_t> retry_weight{"retry_weight", "Accumulator weight for retry received", 0};
    Param<uint8_t> throttle_start{"throttle_start", "Start throttling at this rate", 0};
    Param<int> acc_threshold{
        "acc_threshold", "threshold above which outstanding requests will be referenced by Throttle LUT", 8};

    /**********************
     * Completer Accesors *
     *********************/
    void refresh_counters();
    chi::CBusyState update_cbusy(unsigned tracker_occupancy,
                                 chi::CBusyState rbusy = chi::CBusyState::None,
                                 bool retry_ack = false) const override;
    void update_cntr(chi::ReqEvent event, size_t tracker_occupancy) override;

    /**********************
     * Requester Accesors *
     *********************/
    // Shall be called requester for every chi response and data packet.  Updates internal FSM
    void update(chi::CBusyState cbusy, int outstanding_reqs = -1, bool retry = false) override;

    // Shall be called by requester to determine how many outstanding requests is being allowed
    unsigned num_requests() const override { return out_req_lim; }

    // Called by a requester to determine the limit for outstanding requests before a read must
    // be demoted in its QoS.
    unsigned qos_demote_limit() const override { return qos_demote_lim; }

    chi::CBusyState get_dscbusy() { return ds_cbusy; }

    Event ev_reset{"reset", "Cbusy reset event occured at completer"};
    Event ev_snapshot_rho{"snapshot_rho", "Update rho snapshot based on windowed counters"};
    Event ev_update{"update", "Cbusy reset event occured at requester"};
    Event ev_cbusy_high{"cbusy_high", "Cbusy value of 7, 6 or 5 received"};
    Event ev_cbusy_medium{"cbusy_medium", "Cbusy value of 7, 6 or 5 received"};
    Event ev_cbusy_low{"cbusy_low", "Cbusy value of 7, 6 or 5 received"};
    Event ev_cbusy_send{"cbusy_send", "CBusy value sent from completer"};
    EventData<unsigned> evd_max_outstanding_requests{
        "max_outstanding_requests",
        "Current maximum number of outstanding requests as adjusted by cbusy throttle events"};
    EventData<unsigned> evd_outstanding_requests{"outstanding_requests",
                                                 "Current number of outstanding requests as supplied by the caller"};
    EventData<uint8_t> evd_cbusy_idx{"cbusy_idx", "Cbusy value used to index requester lut"};
    EventData<bool> evd_retry{"retry", "Update was based on a retried request"};
    EventData<unsigned> evd_tracker_occupancy{"tracker_occupancy", "Tracker occupancy"};
    EventData<int> evd_acc_cntr{"acc_cntr", "Accumulator value at requester"};
    EventData<unsigned> evd_comp_cbusy_high{"comp_cbusy_high", "Cbusy value of 7, 6 or 5 sent"};
    EventData<unsigned> evd_comp_cbusy_medium{"comp_cbusy_medium", "Cbusy value of 7, 6 or 5 sent"};
    EventData<unsigned> evd_comp_cbusy_low{"comp_cbusy_low", "Cbusy value of 7, 6 or 5 sent"};
    EventData<float> evd_rho{"rho", "rho value computed at completer"};
    EventData<unsigned> evd_cbusy_state{"cbusy_state", "CBusy state selected at completer"};
    EventData<unsigned> evd_phase{"phase", "Counter phase used to compute rho"};
    EventData<unsigned> evd_allocates{"allocates", "Number of allocate events used in computation of rho"};
    EventData<unsigned> evd_deallocates{"deallocates", "Number of deallocate events used in computation of rho"};

    void end_of_elaboration() override;
    void dump(std::ostream &os) const override;

private:
    float rho_scale_factor{1.0};

    /* Completer CBusy resources */
    std::vector<int> req_recv_cntr;
    std::vector<int> req_cmp_cntr;
    float rho_snapshot{0.0};
    Cycle last_cycle;

    void reset_counters(uint8_t index);
    uint8_t phase(Cycle cycle) const;
    uint64_t phase_delta(Cycle start_cycle, Cycle end_cycle) const;
    void incr_cntr(int &cntr);
    void set_cycle(Cycle cycle) { last_cycle = cycle; }

    std::array<int, util::enum_size<chi::CBusyState>()> num_cbusy;

    /* Requester Cbusy resources */
    chi::CBusyState ds_cbusy;
    uint8_t mask(int cntr, uint8_t high, uint8_t low);
    int counter{0};
    uint8_t current_level{0};
    uint16_t out_req_lim;
    uint16_t qos_demote_lim;
};

} // namespace ampere::mesh
