//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/sc/sc.h"
#include "ampere/archx/driver.h"
#include "ampere/core/l2/crp/l2_replacement_policy.h"
#include "ampere/core/l2/crp/rrip.h"
#include "ampere/core/l2/l2c_pwc.h"
#include "ampere/memory_stream/memory_stream.h"

namespace ampere::core {

using namespace ampere::sc;

class L2Oracle : public implements<L2Oracle, L2RRIP>
{
public:
    explicit L2Oracle(const sc_core::sc_module_name& name);

    Param<bool> predictor_mode{
        "predictor_mode", "Oracle is used to set RRIP age on fill instead of to pick a victim", false};
    Param<bool> oracle_warmup{"oracle_warmup", "Scan L2 warmstart memory stream to plan future replacements", true};
    Param<bool> oracle_trace{"oracle_trace", "Scan instruction stream to plan future replacements", true};

    void touch(uint64_t bank,
               uint64_t set,
               uint64_t way,
               TouchOp op,
               bool demand,
               const Set& set_contents,
               MemReq req = nullptr) override;
    uint64_t victim(uint64_t bank, uint64_t set, const Set& set_contents, uint64_t way_mask) override;

    DriverPort oracle_driver;
    DriverPort real_driver;
    In<MemReq>* lsu_write_request;
    l2::PWC* pwc;
    std::string memory_stream_name;

private:
    void scan_memory_stream();
    void scan_instruction_stream();
    void initialize();
    void oracle_add_pa(PhysAddr pa, uint64_t inum);
    uint64_t next_reference(PhysAddr addr);

    std::map<PhysAddr, std::vector<uint64_t>> oracle_memory;
    bool initialized{false};
    uint64_t touch_count{0};
    uint64_t warmup_access_count{0};

    Event ev_l2_oracle_never_used{"l2_oracle_never_used",
                                  "L2 oracle replacement picked a victim which is never referenced"};
    Event ev_l2_oracle_not_again{"l2_oracle_not_again",
                                 "L2 oracle replacement picked a victim which is not referenced again"};

    Event ev_l2_oracle_new_line{"l2_oracle_new_line", "L2 oracle's initial scan saw a line for the first time"};
    Event ev_l2_oracle_reuse_line{"l2_oracle_reuse_line", "L2 oracle's initial scan saw a line it saw before"};

    Event ev_l2_oracle_pending_write{"l2_oracle_pending_write",
                                     "L2 oracle replacement considered a victim which has a pending write"};
};

} // namespace ampere::core
