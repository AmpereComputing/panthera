//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_MESH_INJECTOR_UOP_SOURCE_H
#define AMPERE_MESH_INJECTOR_UOP_SOURCE_H

#include <memory>

#include "ampere/archx/instruction.h"
#include "ampere/archx/uop.h"
#include "ampere/archx/uop_info.h"
#include "ampere/mesh/mesh_module.h"
#include "ampere/sc/sc.h"

namespace ampere::mesh {

using namespace archx;
using namespace sc;

struct UopObj : public Transaction
{
    uint64_t id;
    std::unique_ptr<archx::Uop> archx_uop;
    std::shared_ptr<Instruction> archx_instruction;
    int block_count{0};
    int latency{1};

    bool ready{false};
    bool complete{false};
    bool str_complete{false};
    bool is_barrier{false};
    bool nonmem_executing{false};

    UopObj(uint64_t id, std::unique_ptr<archx::Uop> archx_uop_, std::shared_ptr<Instruction> archx_instruction)
        : id{id},
          archx_uop{std::move(archx_uop_)},
          archx_instruction{archx_instruction},
          m_address{archx_instruction->physical_address()}
    {
        m_mem_datasize = archx_uop->info->mem_datasize;
        m_end_inst = archx_uop->end_inst > 0;
    }

    bool is_load() const { return archx_uop->info->unit == archx::Uopunit::Load; }
    bool is_store() const { return archx_uop->info->unit == archx::Uopunit::Store; }
    bool is_end_inst() const { return m_end_inst; }

    PhysAddr address() const { return m_address; }
    unsigned mem_datasize() const { return m_mem_datasize; }

    // These fields need to be settable (and separate from the arch_uop) to handle cache line splits.
    void set_address(PhysAddr addr) { m_address = addr; }
    void set_mem_datasize(int size) { m_mem_datasize = size; }
    void set_end_inst(bool value) { m_end_inst = value; }

private:
    PhysAddr m_address;
    int m_mem_datasize;
    bool m_end_inst;
};

struct IOop
{
    IOop(PhysAddr addr, chi::IOType io_type, uint64_t tlp_size, bool barrier)
        : addr{addr}, io_type{io_type}, tlp_size{tlp_size}, barrier{barrier}
    {
    }

    PhysAddr address() const { return addr; }
    chi::IOType type() const { return io_type; }
    uint64_t get_tlp_size() const { return tlp_size; }
    bool is_barrier() const { return barrier; }

private:
    PhysAddr addr;
    chi::IOType io_type;
    uint64_t tlp_size;
    bool barrier{false};
};

using Uop = transaction_ptr<UopObj>;

class UopSource : public MeshModule
{
public:
    explicit UopSource(const sc_core::sc_module_name& name) : MeshModule{name} {}

    virtual std::optional<Uop> fetch_uop() = 0;
    virtual std::optional<IOop> fetch_io() = 0;
    virtual std::pair<chi::IOType, uint64_t> peek_io() = 0;
    virtual std::optional<IOop> fetch_read_io() = 0;
    virtual std::optional<IOop> fetch_write_io() = 0;
    virtual void commit_uop(Uop) = 0;
    virtual uint64_t take_next_id() = 0;

    virtual void enable() { enabled = true; }
    virtual void disable() { enabled = false; }

    void set_node_id(chi::NodeId nid) { node_id = nid; }

    virtual uint64_t get_ping_pong_mask() { return 0; }

protected:
    bool enabled{true};
    chi::NodeId node_id{};
};

} // namespace ampere::mesh

#endif // AMPERE_MESH_INJECTOR_UOP_SOURCE_H
