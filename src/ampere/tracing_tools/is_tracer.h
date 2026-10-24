//
// Copyright (c) 2019-2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <unordered_set>
#include <exception>
#include <cstdio>
#include <fmt/format.h>

#include "ampere/instruction_stream/instruction_stream.h"
#include "ampere/archx/translation.h"
#include "ampere/sc/report.h"
#include "ampere/adis/src/adis.h"
#include "tracer.h"

#define PSTATE_SP (1U)

namespace ampere {
namespace tracer {

class ISTracer : public Tracer
{
public:
    ISTracer() = delete;
    ISTracer(const std::string filename,
             const std::map<archx::SysReg, uint64_t> sysregs,
             const archx::EL el,
             StateAccessor* stateaccessor,
             bool debug,
             bool record_destination_registers)
        : filename(filename),
          tmp_filename(fmt::format("{}.tmp", filename)),
          stateaccessor{stateaccessor},
          debug_mode{debug},
          record_dst_registers{record_destination_registers},
          first_instruction{true}
    {
        is::Features stream_features;
        stream_features.set_memory_translation(true);
        stream_features.set_divide_sqrt_registers(true);
        stream_features.set_cache_maintenance_registers(true);
        stream_features.set_prefetch_registers(true);
        stream_features.set_all_destination_registers(debug_mode || record_dst_registers);
        stream_features.set_memory_values(true);
        stream_features.set_sve_predicated_memops(true);
        stream_features.set_sve_predicate_registers(true);
        stream_features.set_memop_size(true);
        stream_features.set_nzcv_flags_on_instructions(true);
        stream_features.set_dc_zva_addresses(true);

        writer = new is::InstructionStreamWriter(tmp_filename, stream_features);
        assert(writer);

        for (const auto& [sysreg, value] : sysregs)
            sysreg_write(sysreg, value);
        el_change(el, ContextUpdateType::TRACING);
    }
    virtual ~ISTracer() {}

    virtual bool start_measurement() override
    {
        is::Event event;
        is::Control control;
        control.set_type(is::Control_Type_START_MEASUREMENT);
        event.mutable_control()->CopyFrom(control);
        writer->write(event);

        return true;
    }

    virtual bool stop_measurement() override
    {
        is::Event event;
        is::Control control;
        control.set_type(is::Control_Type_STOP_MEASUREMENT);
        event.mutable_control()->CopyFrom(control);
        writer->write(event);

        return true;
    }

    virtual bool stop_tracing(bool sve_observed) override
    {
        // clean up the first writer
        writer->finalize();
        delete writer;

        auto reader = is::InstructionStreamReader(tmp_filename);
        writer = new is::InstructionStreamWriter(filename, reader.features());

        if (unaccounted_writes > 0)
            std::cout << "WARNING: Observed " << unaccounted_writes
                      << " unaccounted-for writes while recording uninitialized loads." << std::endl;

        // First, write memory values for all uninitialized_loads
        for (const auto& [pa, val_size] : uninitialized_loads) {
            is::Event event;
            is::Memory memory;
            memory.mutable_address()->set_pa(pa.value());
            uint64_t val = val_size.first;
            size_t size = val_size.second;
            memory.set_value((char*)&val, size);
            event.mutable_memory()->CopyFrom(memory);
            writer->write(event);
        }

        // Loop over all events in the instruction stream.
        is::Event event;
        bool status = reader.read(event);
        while (status) {
            writer->write(event);
            event.Clear();
            status = reader.read(event);
        }

        writer->finalize();
        delete writer;

        remove(tmp_filename.c_str());

        return true;
        (void)sve_observed;
    }

    virtual void abort_tracing() override
    {
        try {
            writer->finalize();
        } catch (std::exception& e) {
            log("ISTracer caught exception '%s' while finalizing instruction stream\n", e.what());
        }
        delete writer;

        remove(filename.c_str());
        remove(tmp_filename.c_str());
    }

    virtual bool sysreg_write(const archx::SysReg sysreg, uint64_t value) override
    {
        sysregs[sysreg] = Bitfield(value);

        is::Event event;
        is::SysReg is_sysreg;
        is_sysreg.set_type(is::SysReg_Type_WRITE);
        is_sysreg.set_op0(sysreg.op0);
        is_sysreg.set_op1(sysreg.op1);
        is_sysreg.set_crn(sysreg.crn);
        is_sysreg.set_crm(sysreg.crm);
        is_sysreg.set_op2(sysreg.op2);
        is_sysreg.set_value(value);
        event.mutable_sysreg()->CopyFrom(is_sysreg);
        writer->write(event);

        return true;
    }

    virtual bool sysreg_read(const archx::SysReg sysreg, uint64_t value) override
    {
        sysregs[sysreg] = Bitfield(value);

        is::Event event;
        is::SysReg is_sysreg;
        is_sysreg.set_type(is::SysReg_Type_READ);
        is_sysreg.set_op0(sysreg.op0);
        is_sysreg.set_op1(sysreg.op1);
        is_sysreg.set_crn(sysreg.crn);
        is_sysreg.set_crm(sysreg.crm);
        is_sysreg.set_op2(sysreg.op2);
        is_sysreg.set_value(value);
        event.mutable_sysreg()->CopyFrom(is_sysreg);
        writer->write(event);

        return true;
    }

    virtual bool el_change(const archx::EL el, ContextUpdateType update_type) override
    {
        is::Event event;
        is::Context context;
        context.set_type(is::Context_Type_EL);
        context.set_value(el);
        event.mutable_context()->CopyFrom(context);
        writer->write(event);
        current_el = el;

        auto pstate = stateaccessor->read_pstate();
        if (pstate) {
            current_pstate = *pstate;
        }

        auto el_sp = stateaccessor->read_sp(el, current_pstate & PSTATE_SP);
        if (el_sp) {
            // Update last_gpr SP value to new EL value so that we only display actual in-EL changes to SP, not just
            // apparent SP changes due to the EL change
            last_gprs[31] = *el_sp;
        }

        return true;
    }

    is::Instruction first_is_instr;
    virtual void record_initial_state() override
    {
        // record initial state on as *sources* for the first instruction
        auto maybe_gprs = stateaccessor->read_gp_registers();
        if (maybe_gprs) {
            gpr_map_t gprs = maybe_gprs.value();
            for (const auto& [reg, value] : gprs) {
                if (reg != 32) { // reg==32 is the PC and we do not want to store that with the GPRs
                    is::Register* is_reg = first_is_instr.add_register_();
                    is_reg->set_number(reg);
                    is_reg->set_value(value);
                    last_gprs[reg] = value;
                }
            }
        }

        auto maybe_vfprs = stateaccessor->read_vfp_registers();
        if (maybe_vfprs) {
            vec_map_t vfprs = maybe_vfprs.value();
            for (const auto& [reg, value] : vfprs) {
                is::VFPRegister* is_reg = first_is_instr.add_vfp_register();
                is_reg->set_number(reg);
                is_reg->set_ls_value(*((uint64_t*)value.data()));
                is_reg->set_ms_value(*((uint64_t*)(value.data() + 8)));
                last_vfprs[reg] = value;
            }
        }

        auto maybe_predregs = stateaccessor->read_pred_registers();
        if (maybe_predregs) {
            pred_map_t predregs = maybe_predregs.value();
            for (const auto& [reg, value] : predregs) {
                is::PredRegister* is_reg = first_is_instr.add_src_pred_register();
                is_reg->set_number(reg);
                is_reg->set_value(value);
                last_predregs[reg] = value;
            }
        }

        // Store PSTATE.NZCV into output trace
        if (auto pstate = stateaccessor->read_pstate(); pstate.has_value()) {
            std::uint32_t nzcv = pstate.value() & 0xF000'0000;
            first_is_instr.set_src_nzcv(nzcv);
            last_nzcv = nzcv;
        }
    }

    virtual bool instruction_exec(const Instruction& instruction, const ProgramCounter next_pc) override
    {
        /* translate instruction and data VAs to PAs, updating
         * translation-related state */
        handle_translations(instruction);

        // Convert the tracer instruction to an is::Instruction
        is::Instruction is_instr;
        if (first_instruction) {
            is_instr = first_is_instr; // contains initial state
        }
        is_instr.mutable_program_counter()->set_va(instruction.pc.value());
        if (instruction.pa)
            is_instr.mutable_program_counter()->set_pa(instruction.pa.value().value());
        is_instr.set_opcode(instruction.opcode);

        for (const auto& memop : instruction.memops) {
            is::MemOp* is_memop = is_instr.add_memop();
            is_memop->set_va(memop.va.value());
            if (memop.pa)
                is_memop->set_pa(memop.pa.value().value());
            if (memop.predicated_off)
                is_memop->set_predicated_off(true);
            is_memop->set_bytes(memop.bytes);
        }

        if (instruction.exception_class) {
            is_instr.mutable_exception()->set_exception_class(instruction.exception_class.value());
        }

        // TODO For now, the only registers passed into instruction_exec are
        // for TLBI, cache maintenance, divide, and prefetch instructions, so
        // we know we want to pass them all through. One day, we may want to
        // add checks like `if (IS_TLBI(instruction.opcode))`
        for (const auto& [reg, value] : instruction.source_regs) {
            if (reg == 32) { // ignore changes to the PC
                continue;
            }
            is::Register* is_reg = is_instr.add_register_();
            is_reg->set_number(reg);
            is_reg->set_value(value);
        }

        for (const auto& [reg, value] : instruction.source_vfp_regs) {
            is::VFPRegister* is_reg = is_instr.add_vfp_register();
            is_reg->set_number(reg);
            is_reg->set_ls_value(*((uint64_t*)value.data()));
            is_reg->set_ms_value(*((uint64_t*)(value.data() + 8)));
        }

        for (const auto& [reg, value] : instruction.source_pred_regs) {
            is::PredRegister* is_reg = is_instr.add_src_pred_register();
            is_reg->set_number(reg);
            is_reg->set_value(value);
        }

        if (debug_mode || record_dst_registers) {
            auto maybe_gprs = stateaccessor->read_gp_registers();
            auto maybe_vfprs = stateaccessor->read_vfp_registers();
            auto maybe_predregs = stateaccessor->read_pred_registers();
            auto state_el = stateaccessor->read_el();
            if (!maybe_gprs or !maybe_vfprs or !maybe_predregs)
                return false;
            gpr_map_t gprs = maybe_gprs.value();
            vec_map_t vfprs = maybe_vfprs.value();
            pred_map_t predregs = maybe_predregs.value();

            // Store PSTATE.NZCV into output trace
            if (auto pstate = stateaccessor->read_pstate(); pstate.has_value()) {
                std::uint32_t nzcv = pstate.value() & 0xF000'0000;
                if (nzcv != last_nzcv) {
                    is_instr.set_dst_nzcv(nzcv);
                    last_nzcv = nzcv;
                }
            }

            uint64_t effective_sp_val{0};
            for (const auto& [reg, value] : gprs) {
                // Save off gpr sp value because it can be invalid around EL change boundaries
                if (reg == 31) {
                    effective_sp_val = value;
                    continue;
                }
                // ignore changes to the PC
                if (reg == 32)
                    continue;
                if (value != last_gprs[reg]) {
                    is::Register* is_reg = is_instr.add_dst_register();
                    is_reg->set_number(reg);
                    is_reg->set_value(value);
                    last_gprs[reg] = value;
                }
            }
            if (!first_instruction && state_el && (*state_el != current_el)) {
                // In this case the current SP has already been saved off and replaced with the SP for the new EL we are
                // headed to
                auto el_sp = stateaccessor->read_sp(current_el, current_pstate & PSTATE_SP);

                if (el_sp) {
                    effective_sp_val = *el_sp;
                } // If we can't access pstate or sysregs for some reason just use the effective SP value already taken
                  // from the GPR above
            }

            if (effective_sp_val != last_gprs[31]) {
                is::Register* is_reg = is_instr.add_dst_register();
                is_reg->set_number(31);
                is_reg->set_value(effective_sp_val);
                last_gprs[31] = effective_sp_val;
            }

            for (const auto& [reg, value] : vfprs) {
                if (value != last_vfprs[reg]) {
                    is::VFPRegister* is_reg = is_instr.add_dst_vfp_register();
                    is_reg->set_number(reg);
                    is_reg->set_ls_value(*((uint64_t*)value.data()));
                    is_reg->set_ms_value(*((uint64_t*)(value.data() + 8)));
                    last_vfprs[reg] = value;
                }
            }
            for (const auto& [reg, value] : predregs) {
                if (value != last_predregs[reg]) {
                    is::PredRegister* is_reg = is_instr.add_dst_pred_register();
                    is_reg->set_number(reg);
                    is_reg->set_value(value);
                    last_predregs[reg] = value;
                }
            }
            first_instruction = false;
        }

        for (const auto& memop : instruction.memops) {
            if (memop.pa) {
                if (memop.is_store) {
                    // write memory events to instruction stream and record which bytes in memory have been written to
                    uint64_t curr_pa = memop.pa.value().value();
                    size_t size_written = 0;

                    while (curr_pa < memop.pa.value().value() + (uint64_t)memop.bytes) {
                        size_t size_left = ((uint64_t)memop.bytes - size_written) % 8;
                        size_t granularity = (size_left == 0 ? 8 : size_left);
                        uint64_t val = 0;
                        stateaccessor->read_memory(curr_pa, (uint8_t*)(&val), granularity);

                        is::Event event;
                        is::Memory memory;
                        memory.mutable_address()->set_pa(curr_pa);
                        memory.set_value((char*)&val, granularity);
                        event.mutable_memory()->CopyFrom(memory);
                        writer->write(event);

                        update_written_mem_chunks(PhysAddr(curr_pa), granularity);

                        curr_pa += 8;
                        size_written += granularity;
                    }
                }

                else {
                    // If there exists any other store to the same address in this instruction, ignore this load.
                    bool ignore_load = false;
                    PhysAddr target_addr = memop.pa.value();
                    for (unsigned i = 0; i < instruction.memops.size(); i++) {
                        if (instruction.memops[i].is_store && instruction.memops[i].pa
                            && instruction.memops[i].pa.value() == target_addr)
                        {
                            ignore_load = true;
                            break;
                        }
                    }

                    if (!ignore_load) {
                        size_t size_read = 0;
                        PhysAddr curr_pa = target_addr;
                        while (curr_pa < memop.pa.value() + (uint64_t)memop.bytes) {
                            size_t size_left = ((uint64_t)memop.bytes - size_read) % 8;
                            size_t granularity = (size_left == 0 ? 8 : size_left);
                            handle_load_memop(curr_pa, granularity);
                            curr_pa += 8;
                            size_read += granularity;
                        }
                    }
                }
            }
        }

        // Record PTEs for `DC ZVA` and prefetch instructions. They don't have
        // memops since we record the register values instead of addresses
        // associated with memory accesses, so they will not automatically get
        // their PTEs recorded 'for free' with a call to `memory_access()`
        // above.
        const auto adis_info = instruction.adis_info;
        auto c_maint_type = cache_maint_type(adis_info);
        if (is_zva(c_maint_type)) {
            VirtAddr va = VirtAddr(instruction.source_regs.at(MSR_REG(instruction.opcode)));
            Translation tr =
                Translation(va, Bitfield(sysregs[TCR_EL1]), Bitfield(sysregs[TTBR0_EL1]), Bitfield(sysregs[TTBR1_EL1]));
            record_translation(tr); // performs the translation in addition to recording the PTEs accessed

            // Create a memop on the instruction to hold the base virtual and
            // physical addresses for the DC ZVA instrucion
            is::MemOp* is_memop = is_instr.add_memop();
            is_memop->set_va(va.value());

            if (!tr.faulted()) {
                auto pa = tr.result_address();
                is_memop->set_pa(pa.value());

                // TODO This assumes that DCZID_EL0.BS is 6 (block size written
                // by each access is 64 bytes). This matches the current QEMU
                // implementation for all system-mode AArch64 processors.
                PhysAddr aligned_pa = pa & ~((uint64_t)0x3f); // align to 64 bytes
                char val[8] = {0};
                for (uint64_t offset = 0; offset < 64; offset += 8) {
                    is::Event event;
                    is::Memory memory;
                    memory.mutable_address()->set_pa(aligned_pa.value() + offset);
                    memory.set_value(&val, 8 /*length in bytes*/);
                    event.mutable_memory()->CopyFrom(memory);
                    writer->write(event);

                    update_written_mem_chunks(aligned_pa + offset, 8);
                }
            }
        } else if (adis::prefetch(adis_info)) {
            const auto& [ok, va_opt] = prefetch_va("ISTracer", instruction, adis_info);
            if (!ok) {
                return false; // encountered an error
            }
            if (va_opt.has_value()) {
                Translation tr = Translation(va_opt.value(),
                                             Bitfield(sysregs[TCR_EL1]),
                                             Bitfield(sysregs[TTBR0_EL1]),
                                             Bitfield(sysregs[TTBR1_EL1]));
                record_translation(tr);
            }
        }

        // Note that the instruction is intentionally not written until the
        // very end of the function, because it is possible that it is modified
        // from how it was pass in (for example, to add a memop containing the
        // address(es) for a DC ZVA instruction)
        writer->write(is_instr);

        return true;
    }

private:
    /*
       Helper for update_written_mem_chunks below to take care of the fact that the
       actual memory region being written to may span multiple 64B alignments in
       written_mem_chunks data structure.
       Update_written_mem_chunks calls this function for each 64B alignment.
    */
    void raw_update_written_mem_chunks(PhysAddr addr, PhysAddr aligned, size_t size)
    {
        if ((aligned.value() & (uint64_t)0x3f) != 0)
            AMPERE_ERROR("aligned must be 64-byte aligned.");
        if (!((addr >= aligned) && (addr < (aligned + 64))))
            AMPERE_ERROR("addr must lie within the 64-byte range starting at aligned.");

        std::unordered_map<PhysAddr, uint64_t>::iterator gt = written_mem_chunks.find(aligned);
        uint64_t shift = addr - aligned;

        uint64_t bitmap = ((1UL << size) - 1) << shift;
        if (gt != written_mem_chunks.end())
            gt->second |= bitmap;
        else
            written_mem_chunks.emplace(aligned, bitmap);
    }

    /*
       Helper for mem_chunk_written below to take care of the fact that the
       actual written memory region may span multiple 64B alignments in
       written_mem_chunks data structure.
       Mem_chunk_written calls this function for each 64B alignment.
    */
    uint64_t raw_read_mem_chunk_written(PhysAddr addr, PhysAddr aligned, size_t size)
    {
        if ((aligned.value() & (uint64_t)0x3f) != 0)
            AMPERE_ERROR("aligned must be 64-byte aligned.");
        if (!((addr >= aligned) && (addr < (aligned + 64))))
            AMPERE_ERROR("addr must lie within the 64-byte range starting at aligned.");

        std::unordered_map<PhysAddr, uint64_t>::iterator gt = written_mem_chunks.find(aligned);

        if (gt == written_mem_chunks.end()) {
            return 0;
        }

        else {
            Bitfield bitmap = Bitfield(gt->second);
            unsigned lo = addr.value() - aligned.value();
            unsigned hi = lo + size - 1;
            return (bitmap.extract_range(hi, lo));
        }
    }

    // Indicate with a bitmap that each byte from addr to addr+size has been written to
    void update_written_mem_chunks(PhysAddr addr, size_t size)
    {
        if (size > 8)
            AMPERE_ERROR("Invalid size = {}. Max size is 8.", size);

        while (size) {
            size_t written = 0;
            uint64_t aligned = addr.value() & (~(uint64_t)0x3f);
            uint64_t distance_to_boundary = aligned + 64 - addr.value();
            written = std::min(size, distance_to_boundary);
            raw_update_written_mem_chunks(addr, PhysAddr(aligned), written);
            size -= written;
            addr += written;
        }
    }

    // Return a mask representing which bytes from addr to addr+size have been written to
    uint64_t mem_chunk_written(PhysAddr addr, size_t size)
    {
        if (size > 8)
            AMPERE_ERROR("Invalid size = {}. Max size is 8.", size);

        uint64_t mask = 0;
        size_t read_so_far = 0;

        while (size) {
            size_t read = 0;
            uint64_t aligned = addr.value() & (~(uint64_t)0x3f);
            uint64_t distance_to_boundary = aligned + 64 - addr.value();
            read = std::min(size, distance_to_boundary);
            mask |= raw_read_mem_chunk_written(addr, PhysAddr(aligned), read) << read_so_far;
            read_so_far += read;
            size -= read;
            addr += read;
        }

        return mask;
    }

    /*
     Identify any unaccounted for writes leading to inconsistent values
     when classifying uninitialized loads. We encountered unaccounted for writes when dealing with
     atomic instructions on the same address (i.e., load and store for the same address in the same instruction).
     Leaving this function here to raise any future potential occurrences of any other types of unaccounted
     for writes.

     (size1, val1) -> size and value at the address ADDR already in uninitialized_loads map.
     (size2, val2) -> size and value at the address ADDR being loaded currently.
     (new_size, new_val) -> final size, final val that should go into uninitialized_loads map at address ADDR.

     Strategy in this function: always update the value to be that of the unaccounted for write. However, this
     may need to change depending on what future bug causes these unaccounted writes.
    */
    bool unaccounted_write(
        size_t size1, uint64_t val1, size_t size2, uint64_t val2, size_t& new_size, uint64_t& new_val)
    {
        if (size1 == size2) {
            new_size = size2;
            new_val = val2;
            if (val1 != val2) {
                return true;
            }
        }

        else if (size1 > size2)
        {
            new_size = size1;
            uint64_t data = val1 & ((1UL << size2 * 8) - 1);
            if (data != val2) {
                /* need to adjust least significant size2 bytes in val1
                   already residing in uninitialized_loads map.
                   Make it equal to val2 to account for unexplained write. */
                val1 &= ~((1UL << size2 * 8) - 1);
                val1 |= val2;
                new_val = val1;
                return true;
            } else {
                new_val = val1;
            }
        }

        else if (size1 < size2)
        {
            new_size = size2;
            /* Current size is larger than old size already in map.
               Overwrite with the current value to account for any unexplained write */
            new_val = val2;
            uint64_t data = val2 & ((1UL << size1 * 8) - 1);
            if (data != val1)
                return true;
        }

        return false;
    }

    // populate uninitialized loads by checking mask representing which
    // bytes from pa to pa+size have been written to
    void handle_load_memop(PhysAddr pa, size_t size)
    {
        uint64_t mask = mem_chunk_written(pa, size);

        // all bytes from pa to pa+size have been written to
        if (mask == ((1UL << size) - 1))
            return;

        size_t read_size = 0;
        uint64_t read_start_pa;
        bool read_start_pa_set = false;

        /* traverse each byte from pa to pa+size, collecting unwritten bytes to
           adding to uninitialized loads map */
        for (unsigned i = 0; i < size; i++) {
            bool byte_written = mask & (1UL << i);

            // collect consecutive unwritten bytes
            if (!byte_written) {
                read_size++;

                if (!read_start_pa_set) {
                    read_start_pa = pa.value() + i;
                    read_start_pa_set = true;
                }
            }

            // condition 1: add to map at most significant byte if it is was not written
            // condition 2: add to map when we encounter a byte that has already been written
            bool stop_to_read = (!byte_written && i == (size - 1)) || (byte_written && read_start_pa_set);

            if (stop_to_read) {
                uint64_t val = 0;
                stateaccessor->read_memory(read_start_pa, (uint8_t*)(&val), read_size);

                std::map<PhysAddr, std::pair<uint64_t, size_t>>::iterator gt =
                    uninitialized_loads.find(PhysAddr(read_start_pa));
                if (gt != uninitialized_loads.end()) {
                    size_t old_size = gt->second.second;
                    uint64_t old_val = gt->second.first;
                    size_t size_update = 0;
                    uint64_t val_update = 0;

                    bool unw = unaccounted_write(old_size, old_val, read_size, val, size_update, val_update);

                    if (unw)
                        unaccounted_writes++;

                    gt->second.first = val_update;
                    gt->second.second = size_update;
                } else {
                    uninitialized_loads.emplace(read_start_pa, std::make_pair(val, read_size));
                }

                read_size = 0;
                read_start_pa_set = false;
            }
        }
    }

    void record_translation(archx::Translation& translation)
    {
        while (!translation.done()) {
            uint64_t pte;
            PhysAddr pte_addr = translation.next_pte();
            assert(!(pte_addr.value() & 0x7));
            stateaccessor->read_memory(pte_addr.value(), (uint8_t*)(&pte), 8);
            translation.pte_fetched(pte);
            handle_load_memop(pte_addr, 8);
        }
    }

    bool check_translation(archx::Translation& translation, bool faulted, const PhysAddr answer)
    {
        // Check translation for correctness
        if (faulted != translation.faulted()) {
            log("IS tracer's translation of %s at 0x%" PRIx64 " is%s valid while QEMU's is%s valid\n",
                translation.is_instr ? "instruction" : "data",
                translation.va.value(),
                translation.faulted() ? " not" : "",
                faulted ? " not" : "");
            return false;
        } else if (!faulted) {
            auto pa = translation.result_address();
            if (pa != answer) {
                log("IS tracer's translation of %s 0x%" PRIx64 " (0x%" PRIx64 ") does not match QEMU's (0x%" PRIx64
                    ")\n",
                    translation.is_instr ? "instruction" : "data",
                    translation.va.value(),
                    pa.value(),
                    answer.value());
                return false;
            }
        }
        return true;
    }

    bool check_record_translation(archx::Translation& translation, bool faulted, const PhysAddr answer)
    {
        record_translation(translation);
        return check_translation(translation, faulted, answer);
    }

    bool handle_translations(const Instruction& instruction)
    {
        Translation tr = Translation(instruction.pc, sysregs[TCR_EL1], sysregs[TTBR0_EL1], sysregs[TTBR1_EL1]);
        if (!check_record_translation(tr, !instruction.pa.has_value(), instruction.pa ? *instruction.pa : 0x0_pa))
            return false;

        for (const auto& memop : instruction.memops) {
            Translation tr = Translation(memop.va, sysregs[TCR_EL1], sysregs[TTBR0_EL1], sysregs[TTBR1_EL1]);
            if (!check_record_translation(tr, !memop.pa.has_value(), memop.pa ? *memop.pa : 0x0_pa))
                return false;
        }
        return true;
    }

    std::string filename, tmp_filename;
    std::map<archx::SysReg, archx::Bitfield> sysregs;
    is::InstructionStreamWriter* writer = nullptr;
    StateAccessor* stateaccessor;

    /*
       Memory chunks which have been written to during this trace by store MemOps.
       Key: 64B-aligned address
       Value: 64-bit mask representing which bytes within the aligned 64B chunk have been written
    */
    std::unordered_map<PhysAddr, uint64_t> written_mem_chunks;

    /*
       For load addresses which have not been written to by a previous store,
       track the address, value contained at the address, and its size. The Memory
       events corresponding to these "uninitialized" loads are inserted at the
       "beginning of time" in the instruction stream file.
    */
    std::map<PhysAddr, std::pair<uint64_t, size_t>> uninitialized_loads;

    // Safety check for incorrectly recorded uninitialized loads due to unaccounted-for writes
    uint64_t unaccounted_writes{0};

    // EL of most recent context change event
    archx::EL current_el{EL0};
    bool current_pstate{0};

    bool debug_mode = false;
    bool record_dst_registers;
    bool first_instruction;
    gpr_map_t last_gprs;
    pred_map_t last_predregs;
    vec_map_t last_vfprs;
    std::uint32_t last_nzcv{0};
};

} // namespace tracer
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
