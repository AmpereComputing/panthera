//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <deque>
#include <fcntl.h>
#include <set>
#include <sstream>
#include <sys/types.h>
#include <unistd.h>
#include <unordered_map>

#include "ampere/archx/instruction_is.h"
#include "ampere/archx/addr.h"
#include "ampere/event_stream/event_stream_proto.h"
#include "ampere/instruction_stream/instruction_stream.h"
#include "ampere/sc/sc.h"
#include "chip-decoder-map.h"

using namespace ampere;
using namespace ampere::is;

struct Loop
{
    uint64_t size{0};                     // number of instructions in the loop
    archx::ProgramCounter branch_pc{0x0}; // PC of the loop-ending branch (this is somewhat arbitrary for some loops)

    // statistics about one iteration of this loop
    uint64_t taken_branches{0};
    bool conditional_loop_branch{false};
    bool backwards_loop_branch{false};
    bool single_backwards_conditional_loop_branch{false};

    // statistics about all iterations of this loop
    uint64_t loop_instructions{
        0}; // total number of instructions executed in this loop across all iterations (includes "collection")
};

class Loopfinder : public sc::Module
{
public:
    Loopfinder(const sc_core::sc_module_name& name, sc::Clock* clk, sc::Environment* env) : Module{name, clk, env} {}
    void handle_instruction(archx::Instruction& instruction);
    void finalize();

    sc::Param<unsigned> max_loop_size{"max_loop_size", "max size of loops to identify, in instructions", 32};
    sc::Param<bool> only_single_backwards_conditionals{
        "only_single_backwards_conditionals", "limit to only single-backwards-conditional branch loops", false};

private:
    void garbage_collect();

    sc::Event ev_instruction_commit{"instruction_commit", "An instruction is committed"};
    sc::EventData<archx::ProgramCounter> ev_pc{"program_counter", "Program Counter"};
    sc::EventData<archx::inum_t> ev_inum{"inum", "Instruction ID"};
    sc::Event ev_loop_instruction_commit{"loop_instruction_commit",
                                         "An instruction is committed while in an identified loop"};
    sc::Event ev_loop_mode_instruction_commit{
        "loop_mode_instruction_commit",
        "An instruction is committed while in an identified loop *after* the first iteration"};
    sc::Event ev_loop_end{"loop_end", "We fell off the end of a loop"};
    sc::EventData<uint64_t> ev_loop_size{"loop_size", "number of instructions in each iteration of the loop"};
    sc::EventData<uint64_t> ev_loop_instructions{
        "loop_instructions", "total number of instructions in this loop instance across all iterations"};
    sc::EventData<uint64_t> ev_loops_ago{
        "loops_ago",
        "the number of dynamic instructions ago a loop with this branch PC and iteration count was last seen"};

    // Global state
    std::deque<archx::ProgramCounter> inst_queue;
    std::unordered_map<archx::ProgramCounter, std::set<uint64_t>>
        branches; // map branches to the instruction counts at which they were previously seen within inst_queue
    std::unordered_map<archx::ProgramCounter, std::unordered_map<uint64_t, uint64_t>>
        loops;                     // map PCs and loop lengths to last loop_count that combination was seen
    uint64_t instruction_count{0}; // total instructions considered
    uint64_t loop_count{0};        // total number of distinct loop instances identified

    // State for the current loop, if one has been discovered
    std::optional<Loop> loop{std::nullopt};
    uint64_t instructions_since_last_loop_end{0};
};

void Loopfinder::handle_instruction(archx::Instruction& instruction)
{
    ev_instruction_commit(ev_pc(instruction.program_counter()), ev_inum(instruction.inum()));

    auto pc = instruction.program_counter();
    inst_queue.push_front(pc);

    if (loop.has_value()) {
        // If we were previously in a loop...
        if (inst_queue[loop->size] == pc) {
            // ...and are still in a loop, count statistics about that
            loop->loop_instructions++;
            ev_loop_instruction_commit(ev_pc(instruction.program_counter())), ev_inum(instruction.inum());
            if (loop->loop_instructions > loop->size)
                ev_loop_mode_instruction_commit(ev_pc(instruction.program_counter())), ev_inum(instruction.inum());
        } else {
            // ...and are no longer in a loop, record statistics about this
            // loop and clear variables

            uint64_t loops_ago = 0;
            if (loops.contains(loop->branch_pc) && loops[loop->branch_pc].contains(loop->size)) {
                loops_ago = loop_count - loops[loop->branch_pc][loop->size];
            }

            ev_loop_end(ev_pc(loop->branch_pc),
                        ev_loop_size(loop->size),
                        ev_loop_instructions(loop->loop_instructions),
                        ev_loops_ago(loops_ago));

            // we fell off the end of the loop, so reset loop_size. This
            // triggers the rest of the code below to resume looking for a
            // new loop. Also reset other conters.
            loops[loop->branch_pc][loop->size] = loop_count;
            loop_count++;

            loop = std::nullopt;
            instructions_since_last_loop_end = 0;
        }
    }

    if (instruction.is_taken_branch()) {
        auto& branch_entry = branches[pc];

        if (!loop.has_value() && !branch_entry.empty()) {
            // If we're not already looping and have seen this branch
            // previously, try to start. Search starting at youngest entry,
            // test instruction PCs back in time at this interval to see if we
            // can loop.
            for (auto prev_inum = branch_entry.crbegin(); prev_inum != branch_entry.crend(); ++prev_inum) {
                unsigned candidate_loop_size = instruction_count - *prev_inum;
                unsigned candidate_taken_branches = 0;

                if (candidate_loop_size > max_loop_size) {
                    // We are viewing branches at this PC in order from
                    // youngest to oldest, so if we see one that is too far
                    // in the past to fit within our max_loop_size, we
                    // can safely ignore it AND any older than it
                    break;
                }

                // avoid falling off the end of inst_queue and ensure our
                // loop is smaller than our max loop length
                if (candidate_loop_size * 2 > inst_queue.size())
                    continue;

                // check that every instruction PC at this loop length
                // matches for one full iteration previous to now
                bool loop_instructions_match = true;
                for (unsigned i = 0; i < candidate_loop_size; i++) {
                    if (inst_queue[i] != inst_queue[i + candidate_loop_size]) {
                        loop_instructions_match = false;
                        break;
                    }
                    auto it = branches.find(inst_queue[i + candidate_loop_size]);
                    if (it != branches.end() && it->second.contains(instruction_count - i - candidate_loop_size)) {
                        candidate_taken_branches++;
                    }
                }
                if (loop_instructions_match) {
                    loop = Loop{
                        .size = candidate_loop_size,
                        .branch_pc = pc,
                        .taken_branches = candidate_taken_branches,
                        .conditional_loop_branch = archx::is_conditional(instruction.branch_type()),
                        .backwards_loop_branch = instruction.branch_target() < pc,
                    };
                    loop->single_backwards_conditional_loop_branch =
                        (loop->taken_branches == 1 && loop->conditional_loop_branch && loop->backwards_loop_branch);

                    loop->loop_instructions = std::min(instructions_since_last_loop_end, loop->size);

                    // Don't actually loop if this loop type has been disallowed
                    if (only_single_backwards_conditionals && !loop->single_backwards_conditional_loop_branch)
                        loop = std::nullopt;

                    break;
                }
            }
        }
        branch_entry.insert(instruction_count);
    }

    if (instruction_count % 1000 == 0)
        garbage_collect();

    // next two lines belong at end of method
    instruction_count++;
    instructions_since_last_loop_end++;
}

void Loopfinder::garbage_collect()
{
    while (inst_queue.size() > max_loop_size * 2) {
        auto oldest_pc = inst_queue.back();

        // if this PC was a taken branch, remove it from the branches
        auto it = branches.find(oldest_pc);
        uint64_t oldest_icount = instruction_count + 1 - inst_queue.size();
        if (it != branches.end() && it->second.contains(oldest_icount)) {
            it->second.erase(oldest_icount);
        }
        inst_queue.pop_back();
    }
}

void Loopfinder::finalize()
{
    // check if we fell off the end while looping and record stats for that
    // last loop, if so.
    if (loop.has_value()) {
        uint64_t loops_ago = 0;
        if (loops.contains(loop->branch_pc) && loops[loop->branch_pc].contains(loop->size)) {
            loops_ago = loop_count - loops[loop->branch_pc][loop->size];
        }
        loops[loop->branch_pc][loop->size] = loop_count;
        loop_count++;

        ev_loop_end(ev_pc(loop->branch_pc),
                    ev_loop_size(loop->size),
                    ev_loop_instructions(loop->loop_instructions),
                    ev_loops_ago(loops_ago));
    }
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
        AMPERE_ERROR("no instruction stream was given (use -instruction_stream <file>)");

    sc_core::sc_report_handler::set_actions(sc_core::SC_INFO, sc_core::SC_DISPLAY);

    std::vector<std::string> decoder_names = is_tools::get_decoders("max");
    archx::Instruction::set_decoders(decoder_names);

    Loopfinder loopfinder{"loopfinder", &clock, &env};

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
            loopfinder.handle_instruction(last_instr);
            last_instr = current_instr;

            if (instruction_count == max_instructions) {
                sc_core::sc_stop();
                return EXIT_SUCCESS;
            }
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

    loopfinder.finalize();

    sc_core::sc_stop();
    return EXIT_SUCCESS;
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
