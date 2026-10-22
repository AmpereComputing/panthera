//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <unordered_set>
#include <exception>
#include <cstdio>

#include "ampere/memory_stream/memory_stream.h"
#include "ampere/archx/translation.h"
#include "encodings.h"
#include "tracer.h"
#include "cache.h"

namespace ampere {
namespace tracer {

class MSTracer : public Tracer
{
public:
    MSTracer() = delete;
    MSTracer(const std::string filename,
             const std::map<archx::SysReg, uint64_t> sysregs,
             StateAccessor *stateaccessor,
             uint32_t dcache_size_bytes,
             uint32_t icache_size_bytes)
        : filename(filename), sysregs{sysregs}, stateaccessor{stateaccessor}
    {
        writer = new ms::MemoryStreamWriter(filename, dcache_size_bytes, icache_size_bytes);
        assert(writer);

        dcache = new Cache<PhysAddr>(dcache_size_bytes);
        icache = new Cache<PhysAddr>(icache_size_bytes);

        // output start stream event
        ms::Control ms_control;
        ms_control.set_type(ms::Control_Type_START_MEMORY);
        writer->write(ms_control);
    }
    virtual ~MSTracer() {}

    virtual bool start_measurement() { return true; }

    virtual bool stop_measurement() { return true; }

    virtual bool stop_tracing(bool sve_observed)
    {
        ms::Control ms_control;

        // end of normal memory stream
        ms_control.set_type(ms::Control_Type_END_MEMORY);
        writer->write(ms_control);

        // cache flush control event
        ms_control.set_type(ms::Control_Type_START_CACHE_FLUSH);
        writer->write(ms_control);

        // flush the filter caches
        dcache->append_to_memory_stream(*writer);
        icache->append_to_memory_stream(*writer);

        // end of cache flush, end of stream
        ms_control.set_type(ms::Control_Type_END_CACHE_FLUSH);
        writer->write(ms_control);

        delete dcache;
        delete icache;
        delete writer;

        return true;
        (void)sve_observed;
    }

    virtual void abort_tracing()
    {
        delete writer;

        remove(filename.c_str());
    }

    virtual bool sysreg_write(const archx::SysReg sysreg, uint64_t value)
    {
        // Record to help determine which PTEs should be included in the memory
        // stream
        sysregs.emplace(sysreg, value);
        return true;
    }

    virtual bool sysreg_read(const archx::SysReg sysreg, uint64_t value)
    {
        // Record to help determine which PTEs should be included in the memory
        // stream
        sysregs.emplace(sysreg, value);
        return true;
    }

    virtual bool el_change(const archx::EL el, ContextUpdateType update_type) { return true; }

    void do_translation(archx::Translation &translation)
    {
        while (!translation.done()) {
            uint64_t pte;
            PhysAddr pte_addr = translation.next_pte();
            assert(!(pte_addr.value() & 0x7));
            stateaccessor->read_memory(pte_addr.value(), (uint8_t *)(&pte), 8);
            translation.pte_fetched(pte);
        }
    }

    void memory_access(ms::Access_Type type,
                       ms::Access_Source source,
                       PhysAddr pa,
                       uint32_t size,
                       uint64_t offset,
                       ProgramCounter pc,
                       uint64_t va)
    {
        // pick a cache, any cache...
        auto cache = dcache;
        if (ms::Access_Source_INSTRUCTION == source)
            cache = icache;

        if (!cache->lookup(pa, type == ms::Access_Type_WRITE)) {
            cache->refill(pa, source == ms::Access_Source_INSTRUCTION, type == ms::Access_Type_WRITE);

            ms::Access ms_access;

            ms_access.set_type(type);
            ms_access.set_source(source);
            ms_access.set_pa(pa.value());
            ms_access.set_size(size);
            ms_access.set_offset(offset);
            ms_access.set_pc(pc.value());
            ms_access.set_va(va);

            writer->write(ms_access);
        }
    }

    virtual bool instruction_exec(const Instruction &instruction, const ProgramCounter next_pc)
    {
        static uint64_t instruction_count = 0;

        instruction_count++;

        if (instruction.pa) {
            memory_access(ms::Access_Type_READ,
                          ms::Access_Source_INSTRUCTION,
                          instruction.pa.value(),
                          4 /* bytes */,
                          instruction_count,
                          instruction.pc,
                          instruction.pc.value());
        }

        for (const auto &memop : instruction.memops) {
            if (memop.pa) {
                memory_access(memop.is_store ? ms::Access_Type_WRITE : ms::Access_Type_READ,
                              ms::Access_Source_DATA,
                              memop.pa.value(),
                              memop.bytes,
                              instruction_count,
                              instruction.pc,
                              memop.va.value());
            }
        }

        const auto adis_info = instruction.adis_info;
        auto c_maint_type = cache_maint_type(adis_info);
        if (is_zva(c_maint_type)) {
            // Add addresses for DC ZVA instructions, which are not reported
            // as part of `instruction.memops` above
            VirtAddr va = VirtAddr(instruction.source_regs.at(MSR_REG(instruction.opcode)));
            Translation tr =
                Translation(va, Bitfield(sysregs[TCR_EL1]), Bitfield(sysregs[TTBR0_EL1]), Bitfield(sysregs[TTBR1_EL1]));
            do_translation(tr);

            if (!tr.faulted()) {
                auto pa = tr.result_address();
                // TODO This assumes that DCZID_EL0.BS is 6 (block size written
                // by each access is 64 bytes). This matches the current QEMU
                // implementation for all system-mode AArch64 processors.
                memory_access(ms::Access_Type_WRITE,
                              ms::Access_Source_DATA,
                              pa,
                              64 /* bytes */,
                              instruction_count,
                              instruction.pc,
                              va.value());
            }
        } else if (adis::prefetch(adis_info)) {
            // Add target addresses of prefetch instructions to memory stream as
            // loads. TODO Should we change our treatment of these based on the
            // hints encoded in the instruction (i.e. target cache level,
            // streaming)?
            const auto &[ok, va_opt] = prefetch_va("MSTracer", instruction, adis_info);
            if (!ok) {
                return false; // encountered an error
            }
            if (va_opt.has_value()) {
                VirtAddr va = va_opt.value();
                Translation tr = Translation(
                    va, Bitfield(sysregs[TCR_EL1]), Bitfield(sysregs[TTBR0_EL1]), Bitfield(sysregs[TTBR1_EL1]));
                do_translation(tr);
                if (!tr.faulted()) {
                    auto pa = tr.result_address();
                    auto op = prefetch_op(instruction.opcode);
                    memory_access(ms::Access_Type_READ,
                                  op == PrefetchOp::PLI ? ms::Access_Source_INSTRUCTION : ms::Access_Source_DATA,
                                  pa,
                                  1,
                                  instruction_count,
                                  instruction.pc,
                                  va.value());
                }
            }
        }

        // Note that we are not capturing memory traffic here for any
        // instructions which do not create 'memops' other than DC ZVA
        // instructions. We are not currently aware of any other instructions
        // which do not generate memops as we would expect, but note that it
        // may be possible there are some we have overlooked.

        // TODO We do not record accesses for any PTEs in our memory stream
        // because we do not currently filter them through a MMU/TLB model. In
        // the future, we should consider adding such a model and recording
        // memory accesses for PTEs to the memory stream if they miss the TLB.

        return true;
    }

private:
    std::string filename;
    std::map<SysReg, uint64_t> sysregs;
    ms::MemoryStreamWriter *writer = nullptr;
    Cache<PhysAddr> *dcache = nullptr;
    Cache<PhysAddr> *icache = nullptr;
    StateAccessor *stateaccessor = nullptr;
};

} // namespace tracer
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
