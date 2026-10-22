//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <fcntl.h>
#include <sstream>
#include <sys/types.h>
#include <unistd.h>

#include "ampere/archx/instruction_is.h"
#include "ampere/archx/addr.h"
#include "ampere/event_stream/event_stream_proto.h"
#include "ampere/instruction_stream/instruction_stream.h"
#include "ampere/sc/sc.h"
#include "chip-decoder-map.h"

using namespace ampere;
using namespace ampere::is;

class Catus : public sc::Module
{
public:
    Catus(const sc_core::sc_module_name& name, sc::Clock* clk, sc::Environment* env) : Module{name, clk, env} {}
    void handle_instruction(archx::Instruction& instruction);

private:
    sc::Event ev_instruction_commit{"instruction_commit", "An instruction is committed"};
    sc::Event ev_uop_commit{"uop_commit", "A uop is committed"};
    sc::EventData<uint64_t> ev_pc{"program_counter", "Program Counter"};
    sc::EventData<archx::inum_t> ev_inum{"inum", "Instruction ID"};
    sc::EventData<std::string> ev_uopcode{"uopcode", "Uop mnemonic"};
};

void Catus::handle_instruction(archx::Instruction& instruction)
{
    auto uops = instruction.decode();
    if (!uops.size()) {
        fmt::print("instruction 0x{:08x} did not decode", instruction.opcode());
        exit(EXIT_FAILURE);
    }
    for (auto& uop : uops) {
        std::ostringstream uop_name;
        uop_name << uop->opcode;
        ev_uop_commit(ev_pc(instruction.program_counter().value()), ev_uopcode(uop_name.str()));
    }
    ev_instruction_commit(ev_pc(instruction.program_counter().value()), ev_inum(instruction.inum().id()));
}

int sc_main(int argc, char** argv)
{
    sc::Clock clock{"main", {1, sc_core::SC_NS}};
    sc::Environment env{"system"};

    env.parameters.load_from_args(argc, argv);

    sc::Param<std::string> instruction_stream_name{env, "instruction_stream", "path to an instruction stream", ""};
    sc::Param<uint64_t> max_instructions{env,
                                         "max_instructions",
                                         "maximum number of (committed) instructions to simulate *after warmup*",
                                         std::numeric_limits<std::uint64_t>::max()};
    sc::Param<bool> include_warmup{env,
                                   "include_warmup",
                                   "include warmup instructions in run (still doesn't emit event streams until "
                                   "measurement begins unless event_stream_always is `true`)",
                                   false};
    sc::Param<int> event_stream_fd{env, "event_stream_fd", "file descriptor for output event stream", -1};
    sc::Param<int> resp_stream_fd{env, "resp_stream_fd", "file descriptor for input response stream", -1};
    sc::Param<std::string> event_stream_name{env, "event_stream_name", "filename for output event stream", ""};
    sc::Param<bool> event_stream_always{
        env, "event_stream_always", "always capture the event stream, even during warmup", false};

    if (resp_stream_fd != -1 && event_stream_fd != -1)
        env.event_stream = std::make_unique<event_stream::EventStreamProto>(event_stream_fd, resp_stream_fd);
    else if (event_stream_fd != -1)
        env.event_stream = std::make_unique<event_stream::EventStreamProto>(event_stream_fd);
    else if (!event_stream_name.val().empty())
        env.event_stream = std::make_unique<event_stream::EventStreamProto>(event_stream_name);

    if (instruction_stream_name.val().empty())
        AMPERE_ERROR("no instruction stream was given (use -is <file>)");

    sc_core::sc_report_handler::set_actions(sc_core::SC_INFO, sc_core::SC_DISPLAY);

    std::vector<std::string> decoder_names = is_tools::get_decoders("max");
    archx::Instruction::set_decoders(decoder_names);

    Catus top{"top", &clock, &env};

    if (env.parameters.help_requested()) {
        env.parameters.dump_parameters_verbosely(std::cout);
        sc_core::sc_stop();
        return EXIT_SUCCESS;
    }

    if (!env.parameters.check_all_matched())
        throw std::runtime_error{"Not all parameters were matched. Giving up."};

    env.parameters.values_to_event_stream();

    uint64_t instruction_count{0};
    archx::ArchState state;
    InstructionStreamReader reader(instruction_stream_name.val());
    bool measurement_started = reader.version() < 2;
    Event event;

    env.event_stream->start_simulation();
    if (!measurement_started && !event_stream_always)
        env.event_stream->disable();

    archx::InstructionIS last_instr;

    while (reader.read(event)) {
        if (event.has_instruction() && (include_warmup || measurement_started)) {
            instruction_count++;
            Instruction instr = event.instruction();
            archx::InstructionIS current_instr(instr, archx::inum_t(instruction_count), archx::inum_t(0), state);

            last_instr.set_next_pc(current_instr.program_counter());
            top.handle_instruction(last_instr);
            last_instr = current_instr;

            if (instruction_count == max_instructions) {
                sc_core::sc_stop();
                return EXIT_SUCCESS;
            }
            /*
             * The following two `else if` clauses should be un-commented if you want
             * accurate sysreg values or memory contents. Leaving them commented-out speeds
             * up runs for which you do not need them.
                    } else if (event.has_memory()) {
                        const is::Memory &mem = event.memory();
                        archx::PhysAddr pa = archx::PhysAddr(mem.address().pa());
                        state = std::move(state).write(pa, (uint8_t *)mem.value().data(), mem.value().size());
                    } else if (event.has_sysreg()) {
                        const is::SysReg &s = event.sysreg();
                        archx::SysReg sysreg = archx::SysReg(s.op0(), s.op1(), s.crn(), s.crm(), s.op2());
                        state = std::move(state).write(sysreg, s.value());
            */
        } else if (event.has_context()) {
            const is::Context& c = event.context();
            if (c.type() == is::Context::EL) {
                state = std::move(state).set_exception_level(static_cast<archx::EL>(c.value()));
            }
        } else if (event.has_control()) {
            const is::Control& control = event.control();
            if (control.type() == Control::START_MEASUREMENT) {
                measurement_started = true;
                env.event_stream->enable();
            } else if (control.type() == Control::STOP_MEASUREMENT) {
                sc_core::sc_stop();
                return EXIT_SUCCESS;
            }
        }
        event.Clear();
    }

    sc_core::sc_stop();
    return EXIT_SUCCESS;
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
