//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include "ampere/archx/addr.h"
#include "ampere/archx/register.h"
#include "ampere/core/l2/l2c_fb.h"
#include "ampere/core/l2/l2c_line.h"
#include "ampere/core/l2/l2c_opcode.enum.h"
#include "ampere/mesh/mesh_dat.h"
#include "ampere/mesh/mesh_req.h"
#include "ampere/mesh/mesh_rsp.h"
#include "ampere/mesh/mesh_snp.h"
#include "ampere/uarch_common/memreq.h"

#include <deque>
#include <optional>

namespace ampere::core::l2 {

struct InFlightStores
{
    int any{0};
    int exclusive{0};
    int release{0};
    int atomic{0};

    void update(MemOp::BarrierType barrier_type, MemOp::AtomicType atomic_type, int delta);
    unsigned store_types() const;
};

struct OpObj : public Transaction
{
    OpObj(l2::Opcode opcode, int bank, MemReq& req)
        : opcode{opcode}, addr{req->phys_addr.line_address()}, bank{bank}, req{req}
    {
    }

    OpObj(l2::Opcode opcode, int bank, MemReq& req, uint64_t byte_enables)
        : opcode{opcode}, addr{req->phys_addr.line_address()}, bank{bank}, req{req}, byte_enables{byte_enables}
    {
    }

    OpObj(l2::Opcode opcode, int bank, l2::FillBufferEntry& fbe)
        : opcode{opcode}, addr{fbe.line.addr}, bank{bank}, fbid{fbe.id}
    {
    }

    OpObj(l2::Opcode opcode, int bank, mesh::MeshSnp snp) : opcode{opcode}, addr{snp->Addr}, bank{bank}, snp{snp} {}

    OpObj(const OpObj&) = delete;

    l2::Opcode opcode;
    PhysAddr addr;
    int bank;
    int fbid{-1};
    std::optional<int> replay_after_fbid;
    MemReq req;
    mesh::MeshSnp snp;
    FBCredits fb_credits{{}, {}, {}, {}, {}, {}, {}};
    std::optional<uint64_t> byte_enables; // for stores
    uint64_t pwc_id{0};
    std::optional<PhysAddr> scheduled_with_fill;
    LineState data_state{LineState::I};

    struct RequesterResponse
    {
        MemReq req;
        bool has_value = true;
        RequesterResponse(MemReq req, bool has_value = true) : req(req), has_value(has_value) {}
        operator MemReq() const { return req; }
    };
    std::vector<RequesterResponse> return_data;

    struct
    {
        std::optional<mesh::MeshReq> req;
        std::optional<mesh::MeshReq> evict;
        std::optional<mesh::MeshRsp> rsp;
        std::vector<mesh::MeshDat> dat;
    } chi;

    bool has_fb() const { return fbid >= 0; }
    bool atomic() const
    {
        return opcode == Opcode::ATOMIC_LOAD || opcode == Opcode::ATOMIC_STORE || opcode == Opcode::ATOMIC_CAS;
    }
    uint64_t get_byte_enables() const { return byte_enables.value(); }
    bool all_bytes_valid() const { return get_byte_enables() == util::mask(PhysAddr::LINE_SIZE); }
    void respond(LineState state)
    {
        bool has_value = state != LineState::I;
        data_state = state;
        if (has_value || req->origin == MemOp::LSU)
            return_data.emplace_back(req, has_value);
    }
    void respond_invalid() { respond(LineState::I); }
    void respond_to_all(const std::deque<MemReq>& return_data, bool has_value = true)
    {
        this->return_data.assign(return_data.begin(), return_data.end());

        if (!has_value) {
            for (auto& op : this->return_data)
                if (op.req->origin == MemOp::LSU)
                    op.has_value = has_value;
        }
    }

    void fifo_clear() { fb_credits.release(); }

    void dump(std::ostream& os) const { fmt::print(os, " {:14} | {:16} | {:4} | {:4}\n", opcode, addr, bank, fbid); }
};
using Op = transaction_ptr<OpObj>;

} // namespace ampere::core::l2
