//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_TRACING_TLB_H
#define AMPERE_TRACING_TLB_H

#include <inttypes.h>
#include "ampere/archx/page_size.h"
#include "ampere/instruction_stream/instruction_stream.h"
#include "encodings.h"
#include "tracer.h"

namespace ampere {
namespace tlb {

using namespace ampere;
using namespace archx;

class Translation
{
public:
    Translation(uint64_t pa, PageSize ps) : pa{pa}, page_size{ps} {}
    Translation(uint64_t pa, uint8_t ps) : Translation(pa, PageSize(ps)) {}
    uint64_t pa;
    PageSize page_size;
};

class TLBWriter
{
public:
    virtual bool write(const is::Memory& memory) = 0;
    virtual bool write(const is::SysReg& sysreg) = 0;
    virtual bool write(const is::Context& context) = 0;
};

class TLBWriteCombiner : public TLBWriter
{
public:
    TLBWriteCombiner(TLBWriter* writer1, TLBWriter* writer2) : writers{writer1, writer2} {}

    virtual bool write(const is::Memory& memory)
    {
        if (!writers[0]->write(memory))
            return false;
        return writers[1]->write(memory);
    }
    virtual bool write(const is::SysReg& sysreg)
    {
        if (!writers[0]->write(sysreg))
            return false;
        return writers[1]->write(sysreg);
    }
    virtual bool write(const is::Context& context)
    {
        if (!writers[0]->write(context))
            return false;
        return writers[1]->write(context);
    }
    virtual ~TLBWriteCombiner()
    {
        writers[0] = nullptr;
        writers[1] = nullptr;
    }

private:
    TLBWriter* writers[2];
};

class ISTLBWriter : public TLBWriter
{
public:
    ISTLBWriter(is::InstructionStreamWriter* writer) : writer(writer) {}
    virtual bool write(const is::Memory& memory)
    {
        is::Event event;
        event.mutable_memory()->CopyFrom(memory);
        return writer->write(event);
    }
    virtual bool write(const is::SysReg& sysreg)
    {
        is::Event event;
        event.mutable_sysreg()->CopyFrom(sysreg);
        return writer->write(event);
    }
    virtual bool write(const is::Context& context)
    {
        is::Event event;
        event.mutable_context()->CopyFrom(context);
        return writer->write(event);
    }
    virtual ~ISTLBWriter() { writer = nullptr; }

private:
    is::InstructionStreamWriter* writer;
};

class TLBTracker
{
public:
    TLBTracker() : el(3), tcr_el1(0x0), ttbr0_el1(0x0), ttbr1_el1(0x0), writer(nullptr), memory(nullptr), writing(false)
    {
    }

    void set_el(uint8_t value)
    {
        el = value;
        if (writing) {
            is::Context context;
            context.set_type(is::Context_Type_EL);
            context.set_value(value);
            writer->write(context);
        }
    }
    void set_tcr_el1(uint64_t value)
    {
        tcr_el1 = value;
        write_sysreg(TCR_EL1, value);
    }
    void set_ttbr0_el1(uint64_t value)
    {
        ttbr0_el1 = value;
        write_sysreg(TTBR0_EL1, value);
    }
    void set_ttbr1_el1(uint64_t value)
    {
        ttbr1_el1 = value;
        write_sysreg(TTBR1_EL1, value);
    }

    void start_instruction_stream(tracer::MemoryAccessor* tlbmemory, TLBWriter* tlbwriter)
    {
        assert(tlbmemory);
        assert(tlbwriter);

        memory = tlbmemory;
        writer = tlbwriter;
        writing = true;

        clear_descriptor_cache();

        set_el(el);
        write_sysreg(TCR_EL1, tcr_el1);
        write_sysreg(TTBR0_EL1, ttbr0_el1);
        write_sysreg(TTBR1_EL1, ttbr1_el1);
    };

    void stop_instruction_stream()
    {
        writing = false;
        writer = nullptr;
        memory = nullptr;
    }

    std::optional<Translation> translate(uint64_t va, bool is_instr);
    void clear_descriptor_cache();

private:
    uint8_t el;
    uint64_t tcr_el1, ttbr0_el1, ttbr1_el1;
    TLBWriter* writer;
    tracer::MemoryAccessor* memory;
    bool writing;

    std::unordered_map<uint64_t, uint64_t> descriptors;

    void write_sysreg(SysReg reg, uint64_t value);
    uint64_t fetch_descriptor(uint64_t pa);
    void cache_descriptor(uint64_t pa, uint64_t val);
};

} // namespace tlb
} // namespace ampere

#endif // AMPERE_TRACING_TLB_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
