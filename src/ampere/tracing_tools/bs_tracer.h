//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/branch_stream/branch_stream.h"
#include "ampere/archx/branch_type.h"
#include "ampere/archx/instruction_simple.h"
#include "encodings.h"
#include "tracer.h"

namespace ampere {
namespace tracer {

class BSTracer : public Tracer
{
public:
    BSTracer() = delete;
    BSTracer(const std::string filename,
             const std::map<archx::SysReg, uint64_t> sysregs,
             const archx::EL el,
             StateAccessor *stateaccessor)
        : filename(filename), sysregs{sysregs}
    {
        writer = new bs::BranchStreamWriter(filename);
        assert(writer);

        for (const auto &[sysreg, value] : sysregs)
            sysreg_write(sysreg, value);
        el_change(el, ContextUpdateType::TRACING);
    }
    virtual ~BSTracer() {}

    virtual bool start_measurement() { return true; }

    virtual bool stop_measurement() { return true; }

    virtual bool stop_tracing(bool sve_observed)
    {
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
        is::SysReg is_sysreg;
        is_sysreg.set_type(is::SysReg_Type_WRITE);
        is_sysreg.set_op0(sysreg.op0);
        is_sysreg.set_op1(sysreg.op1);
        is_sysreg.set_crn(sysreg.crn);
        is_sysreg.set_crm(sysreg.crm);
        is_sysreg.set_op2(sysreg.op2);
        is_sysreg.set_value(value);
        writer->write(is_sysreg);

        return true;
    }

    virtual bool sysreg_read(const archx::SysReg sysreg, uint64_t value)
    {
        is::SysReg is_sysreg;
        is_sysreg.set_type(is::SysReg_Type_READ);
        is_sysreg.set_op0(sysreg.op0);
        is_sysreg.set_op1(sysreg.op1);
        is_sysreg.set_crn(sysreg.crn);
        is_sysreg.set_crm(sysreg.crm);
        is_sysreg.set_op2(sysreg.op2);
        is_sysreg.set_value(value);
        writer->write(is_sysreg);

        return true;
    }

    virtual bool el_change(const archx::EL el, ContextUpdateType update_type)
    {
        is::Context context;
        context.set_type(is::Context_Type_EL);
        context.set_value(el);
        writer->write(context);

        return true;
    }

    virtual bool instruction_exec(const Instruction &instruction, const ProgramCounter next_pc)
    {
        auto instr = archx::InstructionSimple(instruction.pc, instruction.opcode, next_pc);

        if (instr.is_branch()) {
            auto branch_type = instr.branch_type();
            bool taken = instr.is_taken_branch();

            bs::Branch branch;
            branch.set_type(branchtype_to_protobuf(branch_type));
            branch.set_taken(taken);
            branch.set_pc(instruction.pc.value());
            if (taken)
                branch.set_target(next_pc.value());
            else
                branch.clear_target();

            writer->write(branch);
        }

        return true;
    }

private:
    std::string filename;
    bs::BranchStreamWriter *writer = nullptr;
    std::map<SysReg, uint64_t> sysregs;
};

} // namespace tracer
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
