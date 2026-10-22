//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include "ampere/sc/sc.h"
#include "ampere/mesh/chi/types.h"

namespace ampere::mesh {

using namespace sc;

class ControlModIfc : public sc_core::sc_interface
{
public:
    virtual void op_start(NodeId core_id, uint64_t mask) {};
    virtual bool is_ready(NodeId core_id) const = 0;
    virtual void op_done(NodeId core_id, uint64_t mask) {};
    virtual uint64_t get_ready() const = 0;
    virtual bool is_running(NodeId core_id) const = 0;
};

class ControlMod : public sc::Module, public ControlModIfc
{
public:
    ControlMod(const sc_core::sc_module_name& name);

    void op_start(NodeId core_id,
                  uint64_t mask) override; // set store_process for core_num and reset store_ready for others
    bool is_ready(NodeId core_id) const override;
    void op_done(NodeId core_id,
                 uint64_t mask) override; // reset store_process for core_num and set store_ready for others
    uint64_t get_ready() const override { return ready; }
    bool is_running(NodeId core_id) const override;

private:
    unsigned get_core_num(NodeId core_id) const;
    uint64_t ready;
    NodeId processing_node;
};

} // namespace ampere::mesh
