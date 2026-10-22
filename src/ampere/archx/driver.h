//
// Copyright (c) 2019-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <memory>
#include <string>

#include <sysc/communication/sc_interface.h>
#include <sysc/communication/sc_port.h>

#include "ampere/archx/addr.h"
#include "ampere/archx/sysreg.h"
#include "ampere/archx/el.h"
#include "ampere/archx/instruction.h"
#include "ampere/archx/inum.h"

namespace ampere::archx {

class Driver : public sc_core::sc_interface
{
public:
    /* Return the program counter of the next instruction to execute in the
     * dynamic instruction stream along with whether the current instruction is
     * a taken branch, and advance driver's pointer to the next instruction
     * (therefore, can only be called once for each instruction). */
    virtual ProgramCounter next_pc(bool *is_taken_branch = nullptr) = 0;

    /* If the supplied PC matches that of the next instruction in the
     * right-path dynamic instruction stream, return the corresponding
     * instruction. If it does not match, or the driver is already on the wrong
     * path, return a bogus 'add' instruction with the given PC. */
    virtual std::shared_ptr<Instruction> fetch(ProgramCounter pc) = 0;

    /* Reset the internal state behind next_pc() and fetch() so that they begin
     * fetching from the instruction immediately after the one with the
     * provided inum() or prev_inum() in the dynamic instruction stream.
     *
     * For mispredicted branch, we send in the id of the branch
     * because it completes and recover() should find or create (if
     * wrong path) the next instruction on that path.
     *
     * RAW_SCRUB and CSF_NO_CMT are different; they send in
     * prior(). RAW_SCRUB sends in prior() because we need to redo the
     * load that suffered the RAW_SCRUB.  CSF_NO_CMT (like self
     * synchronizing sysreg writes) wait until they are oldest in ROB,
     * then flush everything younger, then rexecute themselves.
     */
    virtual void recover(inum_t id) = 0;

    /* Notify the driver that the supplied instruction has been committed and
     * that the global state should be updated to reflect that (and that this
     * instruction no longer needs to be tracked) */
    virtual void commit(std::shared_ptr<Instruction> instruction) = 0;

    /* Query the global state as of the last committed instruction */
    virtual uint64_t read_memory(PhysAddr addr) const = 0;
    virtual uint64_t read_sysreg(SysReg sysreg) const = 0;
    virtual EL exception_level() const = 0;
    virtual uint16_t asid() const = 0;
    virtual uint64_t committed_id() const = 0;
    // interface for returning arbitrary stats to modules that can emit events.
    // Used for emulation validation testing
    virtual uint64_t validation(const std::string &key [[maybe_unused]]) const { return 0; }
};

// Dummy driver purely as a placeholder or for testing purposes
class DummyDriver : public Driver
{
public:
    ProgramCounter next_pc(bool *is_taken_branch [[maybe_unused]] = nullptr) override { return {}; }
    std::shared_ptr<Instruction> fetch(ProgramCounter pc [[maybe_unused]]) override { return nullptr; }
    void recover(inum_t id [[maybe_unused]]) override {}
    void commit(std::shared_ptr<Instruction> instruction [[maybe_unused]]) override {}
    uint64_t read_memory(PhysAddr addr [[maybe_unused]]) const override { return {}; }
    uint64_t read_sysreg(SysReg sysreg [[maybe_unused]]) const override { return {}; }
    EL exception_level() const override { return {}; }
    uint16_t asid() const override { return {}; }
    uint64_t committed_id() const override { return {}; }
};

std::unique_ptr<Driver> makeDriver(std::string filename, uint64_t skip_instructions);
std::unique_ptr<Driver> makeDriver(std::string filename,
                                   uint64_t skip_instructions,
                                   bool speculative_memory_image,
                                   bool use_wrong_path_emulation,
                                   std::uint64_t validate_emulation,
                                   std::string mem_image_path);
std::unique_ptr<Driver> makeBSDriver(std::string filename);

using DriverPort = sc_core::sc_port<Driver>;
using DriverPortOpt = sc_core::sc_port<Driver, 1, sc_core::SC_ZERO_OR_MORE_BOUND>;

} // namespace ampere::archx

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
