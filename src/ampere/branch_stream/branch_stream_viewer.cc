//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <fcntl.h>
#include <iostream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <fmt/format.h>

#include "ampere/instruction_stream/instruction_stream.h"
#include "ampere/branch_stream/branch_stream.h"

using namespace ampere;
using namespace ampere::bs;

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::cout << "branch_stream_viewer [branch stream]" << std::endl;
        exit(1);
    }

    BranchStreamReader reader(argv[1]);
    Event event;

    long branch_count = 0, taken_count = 0, sysreg_count = 0, context_count = 0;
    long type_counts[7] = {0};
    while (reader.read(event)) {
        if (event.has_branch()) {
            Branch branch = event.branch();

            branch_count++;
            type_counts[branch.type()]++;
            if (branch.taken())
                taken_count++;

            fmt::print("Branch: {:7} type: {:22} pc: 0x{:016x} target: 0x{:x}\n",
                       branch.taken() ? "taken" : "untaken",
                       Branch::Type_Name(branch.type()),
                       branch.pc(),
                       branch.target());
        } else if (event.has_sysreg()) {
            is::SysReg sysreg = event.sysreg();
            sysreg_count++;
            fmt::print("SysReg: op0: {} op1: {} crn: {} crm: {}, op2: {}, value: 0x{:x}\n",
                       sysreg.op0(),
                       sysreg.op1(),
                       sysreg.crn(),
                       sysreg.crm(),
                       sysreg.op2(),
                       sysreg.value());
        } else if (event.has_context()) {
            is::Context context = event.context();
            context_count++;
            fmt::print("Context: type: {} value: {}\n", is::Context::Type_Name(context.type()), context.value());
        }

        event.Clear();
    }

    std::cout << std::dec << std::endl;
    std::cout << "total branches = " << branch_count << std::endl;
    std::cout << "taken branches = " << taken_count << std::endl;
    std::cout << "conditional direct branches = " << type_counts[Branch_Type_CONDITIONAL_DIRECT] << std::endl;
    std::cout << "unconditional direct branches = " << type_counts[Branch_Type_UNCONDITIONAL_DIRECT] << std::endl;
    std::cout << "unconditional indirect branches = " << type_counts[Branch_Type_UNCONDITIONAL_INDIRECT] << std::endl;
    std::cout << "call direct branches = " << type_counts[Branch_Type_CALL_DIRECT] << std::endl;
    std::cout << "call indirect branches = " << type_counts[Branch_Type_CALL_INDIRECT] << std::endl;
    std::cout << "return branches = " << type_counts[Branch_Type_RETURN] << std::endl;
    std::cout << "total sysregs = " << sysreg_count << std::endl;
    std::cout << "total context events = " << context_count << std::endl;
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
