//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <cstdint>
#include <boost/smart_ptr/local_shared_ptr.hpp>
#include <boost/smart_ptr/make_local_shared_object.hpp>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>

#include "ampere/archx/driver.h"
#include "ampere/archx/instruction_is.h"
#include "ampere/archx/addr.h"
#include "ampere/archx/inum.h"
#include "ampere/event_stream/event_stream_proto.h"
#include "ampere/instruction_stream/instruction_stream.h"
#include "ampere/memory_stream/memory_stream.h"
#include "ampere/sc/environment.h"
#include "ampere/sc/module.h"
#include "ampere/sc/sc.h"

#include "ampere/tools/is_tools/chip-decoder-map.h"

using namespace ampere;
using namespace ampere::is;

struct InstructionStreamRunnerConfig
{
    sc::Param<std::string> instruction_stream_name;
    sc::Param<std::string> memory_stream_name;
    sc::Param<uint64_t> max_instructions;
    sc::Param<uint64_t> instructions_per_cycle;
    sc::Param<uint64_t> cycles_per_instruction;
    sc::Param<uint64_t> heartbeat;
    sc::Param<bool> include_warmup;
    sc::Param<int> event_stream_fd;
    sc::Param<int> resp_stream_fd;
    sc::Param<std::string> event_stream_name;
    sc::Param<bool> event_stream_always;

    explicit InstructionStreamRunnerConfig(sc::Environment& env)
        : instruction_stream_name{env, "instruction_stream", "path to an instruction stream", ""},
          memory_stream_name{env, "memory_stream", "path to a memory stream for warmup", ""},
          max_instructions{env,
                           "max_instructions",
                           "maximum number of (committed) instructions to simulate *after warmup*",
                           std::numeric_limits<std::uint64_t>::max()},
          instructions_per_cycle{env, "instructions_per_cycle", "instructions per cycle", 0},
          cycles_per_instruction{env, "cycles_per_instruction", "cycles per instruction", 0},
          heartbeat{env, "heartbeat", "instructions per heartbeat", 0},
          include_warmup{env,
                         "include_warmup",
                         "include warmup instructions in run (still doesn't emit event streams until measurement "
                         "begins unless event_stream_always is `true`)",
                         true},
          event_stream_fd{env, "event_stream_fd", "file descriptor for output event stream", -1},
          resp_stream_fd{env, "resp_stream_fd", "file descriptor for input response stream", -1},
          event_stream_name{env, "event_stream_name", "filename for output event stream", ""},
          event_stream_always{env, "event_stream_always", "always capture the event stream, even during warmup", false}
    {
    }
};

template<class Base>
class InstructionStreamHandler : public sc::Module
{
public:
    InstructionStreamHandler(const sc_core::sc_module_name& name,
                             sc::Clock* clk,
                             sc::Environment* env,
                             const InstructionStreamRunnerConfig& config)
        : Module{name, clk, env}, config(config)
    {
    }

    constexpr static bool needs_uops() { return true; }
    constexpr static bool needs_sysregs_updates() { return false; }
    constexpr static bool needs_memory_updates() { return false; }

    bool can_process() const { return true; }
    bool is_complete() const { return true; }

    void run_instruction(const boost::local_shared_ptr<archx::Instruction>& instruction)
    {
        static_cast<Base*>(this)->handle_instruction(instruction);

        if constexpr (Base::needs_uops()) {
            auto uops = instruction->decode();
            if (uops.empty()) {
                fmt::print("instruction 0x{:08x} did not decode", instruction->opcode());
                exit(EXIT_FAILURE);
            }

            std::ostringstream uop_name;
            for (const auto& uop : uops) {
                static_cast<Base*>(this)->handle_uop(*uop, instruction);

                uop_name.str(std::string());
                uop_name << uop->opcode;
                event.uop(instruction->program_counter(), event.uopcode(uop_name.str()));
            }
        }
        event.instruction(instruction->program_counter(), event.inum(instruction->inum()));
    }

    void warmup_memory(archx::PhysAddr addr, unsigned int size, bool write, bool code) {}

protected:
    auto make_driver() const { return archx::makeDriver(config.instruction_stream_name, 0); }

private:
    const InstructionStreamRunnerConfig& config;

    struct
    {
        sc::Event instruction{"instruction", "An instruction is read/processed"};
        sc::Event uop{"uop", "A uop is read/processed"};
        sc::EventData<archx::inum_t> inum{"inum", "Instruction ID"};
        sc::EventData<std::string> uopcode{"uopcode", "Uop mnemonic"};
    } event;
};

template<typename T>
class InstructionStreamRunner : sc::Module, InstructionStreamRunnerConfig
{
    static inline bool needs_sysreg_updates = T::needs_sysregs_updates();
    static inline bool needs_memory_updates = T::needs_memory_updates();

    sc::Environment& env;

    std::unique_ptr<T> handler;

    uint64_t instruction_count{0};
    uint64_t warmup_instruction_count{0};
    archx::ArchState state;
    std::unique_ptr<InstructionStreamReader> reader;
    bool measurement_started = false;
    bool measurement_stopped = false;
    boost::local_shared_ptr<archx::InstructionIS> last_instr;

public:
    InstructionStreamRunner(sc::Clock& clock, sc::Environment& env)
        : Module("runner", &clock, &env), InstructionStreamRunnerConfig(env), env(env)
    {
        if (resp_stream_fd != -1 && event_stream_fd != -1)
            env.event_stream = std::make_unique<event_stream::EventStreamProto>(event_stream_fd, resp_stream_fd);
        else if (event_stream_fd != -1)
            env.event_stream = std::make_unique<event_stream::EventStreamProto>(event_stream_fd);
        else if (!event_stream_name.val().empty())
            env.event_stream = std::make_unique<event_stream::EventStreamProto>(event_stream_name);

        AMPERE_ASSERT(instructions_per_cycle == 0 || cycles_per_instruction == 0,
                      "Only one of instructions_per_cycle or cycles_per_instruction should be provided");

        handler = std::make_unique<T>("top", &clock, &env, static_cast<const InstructionStreamRunnerConfig&>(*this));
    }

    void warmup_memory()
    {
        ms::MemoryStreamReader ms_reader(memory_stream_name.val().c_str());
        ms::Access ms_access;
        ms::Event ms_event;
        uint64_t transactions = 0;

        fmt::print(std::cout, "Warmstart: memory warmup started\n");

        while (true) {
            if (1 == ms_reader.version()) {
                if (!ms_reader.ProtobufStreamReader::read(ms_access))
                    break;
            } else {
                if (!ms_reader.read(ms_event))
                    break;
                if (ms_event.has_control())
                    continue;

                AMPERE_ASSERT(ms_event.has_access(), "Memory event has no access");
                ms_access.Swap(ms_event.mutable_access());
            }

            ++transactions;
            auto addr = archx::PhysAddr(ms_access.pa()).line_address();
            bool code = ms_access.source() != ms::Access_Source_DATA;
            bool write = ms::Access_Type_WRITE == ms_access.type();
            handler->warmup_memory(addr, ms_access.size(), write, code);

            ms_access.Clear();
        }

        fmt::print(std::cout, "Warmstart: memory warmup completed {} transactions\n", transactions);
    }

    void setup()
    {
        if (instruction_stream_name.val().empty())
            AMPERE_ERROR("no instruction stream was given (use -is <file>)");

        if (!memory_stream_name.val().empty())
            warmup_memory();

        // Disable throw for non-debug only
#ifdef NDEBUG
        sc_core::sc_report_handler::set_actions(sc_core::SC_ERROR, sc_core::SC_DISPLAY | sc_core::SC_STOP);
        sc_core::sc_report_handler::set_actions(sc_core::SC_FATAL,
                                                sc_core::SC_DISPLAY | sc_core::SC_STOP | sc_core::SC_ABORT);
#endif
        sc_core::sc_report_handler::set_actions(sc_core::SC_INFO, sc_core::SC_DISPLAY);
        sc_core::sc_report_handler::set_handler(ampere::sc::report_handler);

        auto decoder_names = is_tools::get_decoders("max");
        archx::Instruction::set_decoders(decoder_names);

        reader = std::make_unique<InstructionStreamReader>(instruction_stream_name.val());
        measurement_started = reader->version() < 2;

        if (!measurement_started && !event_stream_always) {
            env.event_stream->disable();
            fmt::print(std::cout, "Warmup: instruction warmup started\n");
        }
    }

    void start()
    {
        if (!reader)
            setup();

        AMPERE_INFO("End of elaboration. Starting simulation.");
        sc_core::sc_start();
    }

    bool should_stop() const
    {
        if (measurement_stopped)
            return true;

        if (!include_warmup)
            return instruction_count >= max_instructions;

        return measurement_started && (instruction_count - warmup_instruction_count) >= max_instructions;
    }

    bool run() override
    {
        if (cycles_per_instruction && get_cycle() % cycles_per_instruction != 0)
            return true;

        if (should_stop()) {
            if (handler->is_complete()) {
                sc_core::sc_stop();
                return false;
            }
            return true;
        }

        if (!handler->can_process())
            return true;

        Event event;
        while (reader->read(event)) {
            if (event.has_instruction() && (include_warmup || measurement_started)) {
                // process instruction
                instruction_count++;
                if (!measurement_started)
                    warmup_instruction_count++;
                if (heartbeat && instruction_count % heartbeat == 0) {
                    fmt::print(std::cout,
                               "Heartbeat: {} {:>12} cycles, {:>11} instructions\n",
                               measurement_started ? "Simulation" : "Warmup",
                               fmt::group_digits(get_cycle()),
                               fmt::group_digits(instruction_count));
                }

                const auto& instr = event.instruction();
                auto current_instr = boost::make_local_shared<archx::InstructionIS>(instr, instruction_count, state);

                if (last_instr) {
                    last_instr->set_next_pc(current_instr->program_counter());
                    handler->run_instruction(last_instr);
                }
                last_instr = current_instr;

                if (should_stop()) {
                    return true;
                } else if (cycles_per_instruction
                           || (instructions_per_cycle && instruction_count % instructions_per_cycle == 0))
                {
                    break;
                } else if (!handler->can_process()) {
                    break;
                }

            } else if (needs_memory_updates && event.has_memory()) {
                // process memory update
                const auto& mem = event.memory();
                auto addr = archx::PhysAddr(mem.address().pa());
                state = std::move(state).write(addr, (uint8_t*)mem.value().data(), mem.value().size());
            } else if (needs_sysreg_updates && event.has_sysreg()) {
                // process sysreg update
                const auto& s = event.sysreg();
                archx::SysReg sysreg = archx::SysReg(s.op0(), s.op1(), s.crn(), s.crm(), s.op2());
                state = std::move(state).write(sysreg, s.value());
            } else if (event.has_context()) {
                // process context update
                const auto& c = event.context();
                if (c.type() == is::Context::EL) {
                    state = std::move(state).set_exception_level(static_cast<archx::EL>(c.value()));
                }
            } else if (event.has_control()) {
                // process control change
                const auto& control = event.control();
                if (control.type() == Control::START_MEASUREMENT) {
                    measurement_started = true;
                    env.event_stream->enable();
                    fmt::print(
                        std::cout, "Warmup: instruction warmup completed {} instructions\n", warmup_instruction_count);
                    fmt::print(std::cout, "Simulation: instruction simulation started\n");
                } else if (control.type() == Control::STOP_MEASUREMENT) {
                    measurement_stopped = true;
                    return true;
                }
            }

            event.Clear();
        }

        return true;
    }

    void stop()
    {
        if (sc_core::sc_get_status() != sc_core::SC_STOPPED)
            sc_core::sc_stop();

        if (instruction_count > warmup_instruction_count) {
            fmt::print(std::cout,
                       "Simulation: instruction simulation completed {} instructions\n",
                       instruction_count - warmup_instruction_count);
        } else {
            fmt::print(std::cout, "Warmup: instruction warmup ended at {} instructions\n", warmup_instruction_count);
        }
    }

    static int init_and_run(int argc, char** argv)
    {
        auto& clock = *ampere::sc::Clock::get_root();
        auto& env = *ampere::sc::Environment::get_root();

        env.parameters.load_from_args(argc, argv);
        InstructionStreamRunner runner(clock, env);

        if (env.parameters.help_requested()) {
            env.parameters.dump_parameters_verbosely(std::cout);
            sc_core::sc_stop();
            return EXIT_SUCCESS;
        }

        if (!env.parameters.check_all_matched())
            throw std::runtime_error{"Not all parameters were matched. Giving up."};

        bool error = false;
        try {
            runner.start();
        } catch (std::runtime_error& err) {
            std::cerr << err.what() << std::endl;
            error = true;
        }
        runner.stop();

        return error ? EXIT_FAILURE : EXIT_SUCCESS;
    }
};

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
