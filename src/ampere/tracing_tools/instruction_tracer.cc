//
// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <cstdint>
#include <optional>
#include <stdbool.h>
#include <fcntl.h>
#include <glib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <nlohmann/json.hpp>
#include <fmt/format.h>

#include "context.h"
#include "encodings.h"
#include "tracer.h"
#include "perf_semihosting.h"
#include "trace_semihosting.h"
#include "tracing_hints.h"
#include "ampere/instruction_stream/instruction_stream.h"
#include "ampere/archx/instruction_is.h"
#include "sveagen.h"

using namespace ampere;
using namespace tracer;
using nlohmann::json;

extern "C" {

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

/* Start/stop tracing trigger mechanisms */
enum trigger {
    INSTRUCTION, // instruction count
    SEMIHOSTING, // angel semihosting `hlt #0x38ce` calls
    HINT,        // `hint #0x55` calls
    PC           // Nth time PC is called
};

/*
 * Current state of the tracer
 *
 * Each tracer typically transitions through all of the states below, in order
 * from top to bottom. A tracer may not be enabled for a given state, or a
 * state may be so brief as to be nonexistent (i.e. warmup set to 0
 * instructions), but every state should be passed through.
 */
enum tracing_state {
    WARMING_OFFLINE = 1, // starting trigger observed, collecting collateral for structure side-loading
    WARMING_ONLINE = 2,  // online warmup, collecting collateral for online warmup (running model, not collecting stats)
    MEASURING = 4,       // tracing 'interesting' section of workload
    COOLING = 8,         // stopping trigger observed, tracing cooldown_instructions
    STOPPING = 16,       // cooldown_instructions passed, only waiting to stop the tracer
};

struct TracerInfo
{
    std::string id;
    tracer::Tracer::Type type;
    std::unique_ptr<tracer::Tracer> tracer;

    /* States to trace with this tracer (bitwise OR of tracing_state enum
     * values). Note that if a tracer begins tracing and stops that it will be
     * removed. I.e. if these are not contiguous states, only the first block
     * of states will get traced. */
    unsigned trace_states{WARMING_ONLINE | MEASURING | COOLING};
    bool starting_tracing{false};
    uint64_t starting_tracing_at{0}; // copy of total_instruction_count at the point starting_tracing was last set
    bool tracing{false};
    bool stopping_tracing{false};

    // Current state of this tracer
    tracing_state state{WARMING_OFFLINE};

    // Used as starting offsets for determining stopping conditions
    uint64_t initial_instruction_count{0};                 // offset from traced_instructions
    uint64_t initial_trigger_stop_after_pc_occurrences{0}; // offset from trigger_stop_after_pc_occurrences
    bool sve_mode{false};
    const char *type_str() const { return Tracer::type_str(type); }
};

/* The following block of variables are all set from arguments, and should not
 * be otherwise modified */
static const char *trace_filename_base = "trace";
static bool trace_is = true;
static bool trace_elf = false;
static uint64_t max_traces = 1;
static bool profile_el1 = false;
static bool skip_async = false;
static bool profile_children = false;
static char *profile_comm = nullptr;
static bool waiting_for_profile_comm = true;
std::set<uint64_t> profiling_pids; // exception to the rule, can be modified by perf semihosting
static bool profile_all_pids = false;
static bool flat_map = false;
static uint64_t elf_entry_point = 0x40000000;
static bool debug_mode = false;
static bool record_destination_registers = true;
static bool sve_mode = false;
static bool sve_observed = false;
static uint32_t dcache_filter_size_bytes = 65536;
static uint32_t icache_filter_size_bytes = 32768;
static uint64_t elf_section_span = 1024;
std::uint32_t sve_agen_debug = 0;

static const char *bbv_gen_log = nullptr;
static const char *checkpoint_tag = nullptr;
static bool force_stop_trigger = true;

/* Triggers to start tracing (transition to WARMING_OFFLINE) */
static trigger start_trigger = INSTRUCTION;
static long unsigned trigger_start_after_instructions = 0;
static long unsigned trigger_periodically_after_instructions = 0;
bool trigger_start_after_instructions_set = false;
bool trigger_periodically_after_instructions_set = false;
static uint64_t trigger_start_after_pc;
static long unsigned trigger_start_after_pc_occurrences = 0;

/* Triggers to transition to online warmup (transition from WARMING_OFFLINE to
 * WARMING_ONLINE) */
static trigger start_online_warmup_trigger = INSTRUCTION;
static long unsigned trigger_online_after_offline_instructions = 0;
bool trigger_online_warmup_with_instructions_set = false;

/* Triggers to transition to measurement section (transition from
 * WARMING_ONLINE to MEASURING) */
static trigger start_measurement_trigger = INSTRUCTION;
static long unsigned trigger_measurement_after_online_instructions = 0;
bool trigger_measurement_with_instructions_set = false;

/* Triggers to stop tracing (transition from MEASURING to COOLING) */
static trigger stop_trigger = INSTRUCTION;
static long unsigned trigger_stop_after_measurement_instructions = 1000000;
bool trigger_stop_with_instructions_set = false;
static uint64_t trigger_stop_after_pc;                      // PC to trigger on
static long unsigned trigger_stop_after_pc_occurrences = 0; // Number of times having seen PC before triggering stop

/* Triggers to end cooldown (transition from COOLING) */
static long unsigned cooldown_instructions = 10000;

/* Tracking (not set by arguments) */
static qemu_plugin_id_t plugin_id;
static unsigned current_el = 3;
static uint64_t current_pid = 0;
static bool watching = false; /* In the correct PID (and EL, if filtering on that) */
static long unsigned start_pcs_counted = 0;
static long unsigned stop_pcs_counted = 0;
static bool hint_semihosting_stop_trigger_seen = false;
static bool hint_start_online_warmup_seen = false;
static bool hint_start_measure_seen = false;
static uint64_t total_instruction_count = 0; // total number of instructions which have been executed
static uint64_t watched_instructions = 0; // number of instructions which have been executed in the traced PID(s)/EL(s)
static uint64_t last_trace_watched_instructions =
    0; // watched_insructions count at which the last trace was begun (or 0 if the first has not yet been begun)
static uint64_t traced_instructions = 1; // number of instructions which have been executed while tracers.size() > 0 in
                                         // the traced PID(s)/EL(s). Start counting at 1 to allow 0 to mean 'unset'
static uint64_t trace_number = 0;
static Context *context = nullptr;
static tracer::QEMUStateAccessor *stateaccessor = nullptr;
static std::vector<struct TracerInfo> tracers;
static bool ignoring_async = false;
static bool resuming_from_async = false;

/* Collect state before writing it */
static std::optional<tracer::Instruction> last_instruction{std::nullopt};
static std::optional<unsigned> new_el;
static ContextUpdateType el_update_type;
static std::optional<std::pair<SysReg, uint64_t>> sysreg_write;
static std::optional<std::pair<SysReg, uint64_t>> sysreg_read;

// Forward declarations of necessary functions
void plugin_reset_callback(qemu_plugin_id_t id);

static void allocate_state_accessor()
{
    if (!stateaccessor) {
        stateaccessor = new tracer::QEMUStateAccessor();
    }
}

void usage()
{
    log("\n" __FILE__ R"( Usage:
General options:
  trace-filename-base=FILE
        Prefix path where the instruction trace(s) should be
        stored (default: "trace")
  trace-is=[true|false]
        Trace to an instruction stream (default: true)
  trace-elf=[true|false]
        Trace to an ELF (default: false)
  max-traces=MAX
        Exit if MAX traces have been captured (default %lu)
  cooldown-instructions=INSTRS
        Number of instructions to capture after inserting a
        STOP_MEASUREMENT marker at the end of the region of
        interest (default %lu)
  profile-el1=[true|false]
        Profile EL1 in addition to EL0 when counting instructions
        (default: false)
  profile-comm=COMM
        Restrict tracing to the first PID found with this command
        name (used with instruction and PC triggers)
  profile-pid=PID
        Restrict tracing to the given PID (used with instruction
        and PC triggers)
  profile-children=[true|false]
        For profile-comm= or profile-pid=, profile any child
        processes in addition to the parent. This is recursive -
        all descendent processes will be profiled. (default: false)
  flat-map=[true|false]
        Instead of preserving the existing page tables, convert
        them to a flat mapping (only affects ELF tracer) (default: false)
  elf-entry-point=ENTRY_POINT
        The first entry point to consider for the generated ELF file
        (both PA and VA) (default: 0x%)" PRIx64 R"()
  elf-section-span=NUMBER
        Allowed span of zeros in an ELF section.  A larger number
        generally results in fewer sections if they are sparsely
        populated with nonzero values. Increase this when the ELF
        tracer tries to create > 64K sections, the ELF limit.
        (default %lu)
  skip-async=[true|false]
        skip over tracing any instructions after an async until
        the next context change
        (default: false)
  debug=[true|false]
        Enable debug prints for OS forks, context switches,
        and register values. (default: false)
  destination-registers=[true|false]
        Enable saving destination register values in instruction streams
        (default: true)
  dcache-filter-size=BYTES
        Size of dcache in bytes through which to filter memory
        stream accesses. Only data accesses which miss this dcache
        will show up in the stream (default %lu)
  icache-filter-size=BYTES
        Size of cache in bytes through which to filter memory
        stream accesses. Only code accesses which miss this icache
        will show up in the stream (default %lu)
  sve=[true|false]
        Store SVE predicates by default. Will still store the predicates
        if SVE instructions are observed during tracing. (default: false)
  sve-agen-debug=LEVEL
        Debug level 0...10(default %lu)

Simpoint tracing options
  generation-log=JSON_FILE
        Path to read the BBV generation metadata from (must also
        supply checkpoint-tag= if using this option)
  checkpoint-tag=TAG
        The name of the checkpoint being restored, referenced in
        the generation log JSON file.
  override-simpoint-stop-trigger=[true|false]
        Allow the user to set the stop trigger on the command-line
        instead of reading it from the generation log (default: false)

Start triggering options (select only one method)
  start-tracing-after-instructions=INSTRS
        Initially start tracing after INSTRS instructions have
        executed in PID. This is the default start trigger.
        (default %lu)
  start-tracing-periodically-after-instructions=INSTRS
        Start tracing again after INSTRS instructions have
        executed in PID since the previous trace began. This does
        not apply to the first trace (default %lu)
  start-tracing-with-semihosting=[true|false]
        Start tracing when `hlt #0x38ce` is seen with x0 = 1 (default: false)
  start-tracing-after-hint=[true|false]
        Start tracing when `hint #0x55` is seen with x0 = 1 (default: false)
  start-tracing-after-pc=PC
        Start tracing when PC is seen in PID for the Nth time (N
        is defined by start-tracing-after-pc-occurrences=).
  start-tracing-after-pc-occurrences=N
        Count for start-tracing-after-pc= (default %lu)

Start online warmup triggering options (select only one method)
  start-online-warmup-after-offline-warmup-instructions=INSTRS
        Number of instructions worth of data to capture after start
        trigger before online warmup instructions. Setting this
        currently causes both memory and branch streams to be
        captured. (default %lu)
  start-online-warmup-after-hint=[true|false]
        Start tracing when `hint #0x55` is seen with x0 = 4 (default: false).
        Cannot be used with start-online-warmup-after-offline-warmup-instructions.

Start-measurement triggering options (select only one method)
  start-measuring-after-online-warmup-instructions=INSTRS
        Number of instructions to capture after offline warmup
        before inserting a START_MEASUREMENT marker. (default %lu)
  start-measuring-after-hint=[true|false]
        Start tracing when `hint #0x55` is seen with x0 = 3 (default: false).
        Cannot be used with start-measuring-after-online-warmup-instructions.

Stop triggering options (select only one method)
  stop-tracing-after-measuring-instructions=INSTRS
        Stop tracing INSTRS instructions after starting. This is
        the default stop trigger. (default %lu)
  stop-tracing-with-semihosting=[true|false]
        Stop tracing when `hlt #0x38ce` is seen with x0 = 2 (default: false)
  stop-tracing-after-hint=[true|false]
        Stop tracing when `hint #0x55` is seen with x0 = 2 (default: false)
  stop-tracing-after-pc=PC
        Stop tracing when PC is seen in PID for the Nth time after
        starting (N is defined by stop-tracing-after-pc-occurrences=).
  stop-tracing-after-pc-occurrences=N
        Count for stop-tracing-after-pc= (default %lu)
)",
        max_traces,
        cooldown_instructions,
        elf_entry_point,
        elf_section_span,
        dcache_filter_size_bytes,
        icache_filter_size_bytes,
        sve_agen_debug,
        trigger_start_after_instructions,
        trigger_periodically_after_instructions,
        trigger_start_after_pc_occurrences,
        trigger_online_after_offline_instructions,
        trigger_measurement_after_online_instructions,
        trigger_stop_after_measurement_instructions,
        trigger_stop_after_pc_occurrences);
}

inline bool trigger_set(trigger trigger_value, bool instruction_set)
{
    return trigger_value != INSTRUCTION || instruction_set;
}

bool parse_args(int argc, char **argv)
{
    char *endptr;
    for (int i = 0; i < argc; i++) {
        char *opt = argv[i];
        g_autofree char **tokens = g_strsplit(opt, "=", 2);
        if (g_strcmp0(tokens[0], "trace-filename-base") == 0) {
            trace_filename_base = strdup(tokens[1]);
            if (!trace_filename_base) {
                perror("Couldn't allocate memory for trace_filename_base\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "flat-map") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &flat_map)) {
                log("Error: Couldn't parse argument to flat-map= as a boolean\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "elf-entry-point") == 0) {
            elf_entry_point = strtoul(tokens[1], &endptr, 16);
            if (!elf_entry_point && endptr == tokens[1]) {
                log("Error: Couldn't parse argument to elf-entry-point= as hexadecimal\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "trace-is") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &trace_is)) {
                log("Error: Couldn't parse argument to trace-is= as a boolean\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "trace-elf") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &trace_elf)) {
                log("Error: Couldn't parse argument to trace-elf= as a boolean\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "max-traces") == 0) {
            max_traces = strtoul(tokens[1], &endptr, 10);
            if (!max_traces) {
                log("Error: Couldn't parse argument to max-traces= as a decimal\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "profile-el1") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &profile_el1)) {
                log("Error: Couldn't parse argument to profile-el1= as a boolean\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "skip-async") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &skip_async)) {
                log("Error: Couldn't parse argument to skip-async= as a boolean\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "profile-comm") == 0) {
            profile_comm = strdup(tokens[1]);
            if (!profile_comm) {
                perror("Couldn't allocate memory for profile_comm\n");
                return false;
            }
            if (strchr(profile_comm, '/')) {
                log("Error: profile-comm argument cannot have path slashes ('/')\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "profile-pid") == 0) {
            uint64_t profile_pid = strtoul(tokens[1], &endptr, 10);
            if (!profile_pid && endptr == tokens[1]) {
                log("Error: Couldn't parse argument to profile-pid= as a decimal\n");
                return false;
            }
            profiling_pids.emplace(profile_pid);
        } else if (g_strcmp0(tokens[0], "profile-children") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &profile_children)) {
                log("Error: Couldn't parse argument to profile-children= as a boolean\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "generation-log") == 0) {
            bbv_gen_log = strdup(tokens[1]);
            if (!bbv_gen_log) {
                perror("Couldn't allocate memory for bbv_gen_log\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "checkpoint-tag") == 0) {
            checkpoint_tag = strdup(tokens[1]);
            if (!checkpoint_tag) {
                perror("Couldn't allocate memory for checkpoint_tag\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "override-simpoint-stop-trigger") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &force_stop_trigger)) {
                log("Error: Couldn't parse argument to override-simpoint-stop-trigger= as a boolean\n");
                return false;
            }
            force_stop_trigger = !force_stop_trigger; // stored variable is inverse of option name
        } else if (g_strcmp0(tokens[0], "start-tracing-after-instructions") == 0) {
            if (trigger_set(start_trigger, trigger_start_after_instructions_set)) {
                log("Error: Only one start-tracing trigger may be specified\n");
                return false;
            }
            start_trigger = INSTRUCTION;
            trigger_start_after_instructions = strtoul(tokens[1], &endptr, 10);
            if (!trigger_start_after_instructions && endptr == tokens[1]) {
                log("Error: Couldn't parse argument to start-tracing-after-instructions= as a decimal\n");
                return false;
            }
            trigger_start_after_instructions_set = true;
        } else if (g_strcmp0(tokens[0], "start-tracing-periodically-after-instructions") == 0) {
            trigger_periodically_after_instructions = strtoul(tokens[1], &endptr, 10);
            if (!trigger_periodically_after_instructions && endptr == tokens[1]) {
                log("Error: Couldn't parse argument to start-tracing-periodically-after-instructions= as a decimal\n");
                return false;
            }
            trigger_periodically_after_instructions_set = true;
        } else if (g_strcmp0(tokens[0], "start-tracing-with-semihosting") == 0) {
            if (trigger_set(start_trigger, trigger_start_after_instructions_set)) {
                log("Error: Only one start-tracing trigger may be specified\n");
                return false;
            }
            start_trigger = SEMIHOSTING;
        } else if (g_strcmp0(tokens[0], "start-tracing-after-hint") == 0) {
            if (trigger_set(start_trigger, trigger_start_after_instructions_set)) {
                log("Error: Only one start-tracing trigger may be specified\n");
                return false;
            }
            start_trigger = HINT;
        } else if (g_strcmp0(tokens[0], "start-tracing-after-pc") == 0) {
            if (trigger_set(start_trigger, trigger_start_after_instructions_set)) {
                log("Error: Only one start-tracing trigger may be specified\n");
                return false;
            }
            start_trigger = PC;
            trigger_start_after_pc = strtoul(tokens[1], &endptr, 16);
            if (!trigger_start_after_pc && endptr == tokens[1]) {
                log("Error: Couldn't parse argument to start-tracing-after-pc= as hex\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "start-tracing-after-pc-occurrences") == 0) {
            trigger_start_after_pc_occurrences = strtoul(tokens[1], &endptr, 10);
            if (!trigger_start_after_pc_occurrences) {
                log("Error: Couldn't parse argument to start-tracing-after-pc-occurrences= as non-zero decimal\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "start-online-warmup-after-offline-warmup-instructions") == 0) {
            if (trigger_set(start_online_warmup_trigger, trigger_online_warmup_with_instructions_set)) {
                log("Error: Only one start-online-warmup trigger may be specified\n");
                return false;
            }
            trigger_online_after_offline_instructions = strtoul(tokens[1], &endptr, 10);
            if (!trigger_online_after_offline_instructions && endptr == tokens[1]) {
                log("Error: Couldn't parse argument to start-online-warmup-after-offline-warmup-instructions= as a "
                    "decimal\n");
                return false;
            }
            trigger_online_warmup_with_instructions_set = true;
        } else if (g_strcmp0(tokens[0], "start-online-warmup-after-hint") == 0) {
            if (trigger_set(start_online_warmup_trigger, trigger_online_warmup_with_instructions_set)) {
                log("Error: Only one start-online-warmup trigger may be specified\n");
                return false;
            }
            start_online_warmup_trigger = HINT;
        } else if (g_strcmp0(tokens[0], "start-measuring-after-online-warmup-instructions") == 0) {
            if (trigger_set(start_measurement_trigger, trigger_measurement_with_instructions_set)) {
                log("Error: Only one start-measurement trigger may be specified\n");
                return false;
            }
            start_measurement_trigger = INSTRUCTION;
            trigger_measurement_after_online_instructions = strtoul(tokens[1], &endptr, 10);
            if (!trigger_measurement_after_online_instructions && endptr == tokens[1]) {
                log("Error: Couldn't parse argument to start-measuring-after-online-warmup-instructions= as a "
                    "decimal\n");
                return false;
            }
            trigger_measurement_with_instructions_set = true;
        } else if (g_strcmp0(tokens[0], "start-measuring-after-hint") == 0) {
            if (trigger_set(start_measurement_trigger, trigger_measurement_with_instructions_set)) {
                log("Error: Only one start-measurement trigger may be specified\n");
                return false;
            }
            start_measurement_trigger = HINT;
        } else if (g_strcmp0(tokens[0], "stop-tracing-after-measuring-instructions") == 0) {
            if (trigger_set(stop_trigger, trigger_stop_with_instructions_set)) {
                log("Error: Only one stop-tracing trigger may be specified\n");
                return false;
            }
            stop_trigger = INSTRUCTION;
            trigger_stop_after_measurement_instructions = strtoul(tokens[1], nullptr, 10);
            if (!trigger_stop_after_measurement_instructions)
                log("Error: Couldn't parse argument to stop-tracing-after-measuring-instructions= as a decimal\n");
            trigger_stop_with_instructions_set = true;
        } else if (g_strcmp0(tokens[0], "stop-tracing-with-semihosting") == 0) {
            if (trigger_set(stop_trigger, trigger_stop_with_instructions_set)) {
                log("Error: Only one stop-tracing trigger may be specified\n");
                return false;
            }
            stop_trigger = SEMIHOSTING;
        } else if (g_strcmp0(tokens[0], "stop-tracing-after-hint") == 0) {
            if (trigger_set(stop_trigger, trigger_stop_with_instructions_set)) {
                log("Error: Only one stop-tracing trigger may be specified\n");
                return false;
            }
            stop_trigger = HINT;
        } else if (g_strcmp0(tokens[0], "stop-tracing-after-pc") == 0) {
            if (trigger_set(stop_trigger, trigger_stop_with_instructions_set)) {
                log("Error: Only one stop-tracing trigger may be specified\n");
                return false;
            }
            stop_trigger = PC;
            trigger_stop_after_pc = strtoul(tokens[1], &endptr, 16);
            if (!trigger_stop_after_pc && endptr == tokens[1]) {
                log("Error: Couldn't parse argument to stop-tracing-after-pc= as hex\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "stop-tracing-after-pc-occurrences") == 0) {
            trigger_stop_after_pc_occurrences = strtoul(tokens[1], &endptr, 10);
            if (!trigger_stop_after_pc_occurrences) {
                log("Error: Couldn't parse argument to stop-tracing-after-pc-occurrences= as non-zero decimal\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "cooldown-instructions") == 0) {
            cooldown_instructions = strtoul(tokens[1], &endptr, 10);
            if (!cooldown_instructions && endptr == tokens[1]) {
                log("Error: Couldn't parse argument to cooldown-instructions= as a decimal\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "debug") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &debug_mode)) {
                log("Error: Couldn't parse argument to debug= as a boolean\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "destination-registers") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &record_destination_registers)) {
                log("Error: Couldn't parse argument to destination-registers= as a boolean\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "sve") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &sve_mode)) {
                log("Error: Couldn't parse argument to sve= as a boolean\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "dcache-filter-size") == 0) {
            dcache_filter_size_bytes = strtoul(tokens[1], &endptr, 10);
            if (!dcache_filter_size_bytes) {
                log("Error: Couldn't parse argument to dcache-filter-size= as non-zero decimal\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "icache-filter-size") == 0) {
            icache_filter_size_bytes = strtoul(tokens[1], &endptr, 10);
            if (!icache_filter_size_bytes) {
                log("Error: Couldn't parse argument to icache-filter-size= as non-zero decimal\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "elf-section-span") == 0) {
            elf_section_span = strtoul(tokens[1], &endptr, 10);
        } else if (g_strcmp0(tokens[0], "sve-agen-debug") == 0) {
            sve_agen_debug = strtoul(tokens[1], &endptr, 10);
        } else {
            log("Error: Unrecognized option: '%s'\n", opt);
            usage();
            return false;
        }
    }

    if ((start_trigger == PC || start_trigger == INSTRUCTION || stop_trigger == PC || stop_trigger == INSTRUCTION)
        && (profiling_pids.size() && profile_comm))
    {
        log("Specify exactly one of profile-pid or profile-comm when using PC or instruction triggers\n");
        usage();
        return false;
    }
    // assume we're profiling all PIDs if PID is otherwise unspecified
    if (!profiling_pids.size() && !profile_comm) {
        profile_all_pids = true;
    }
    if (!profiling_pids.size() && !profile_comm && profile_children) {
        log("profile-children can only be used in conjunction with profile-pid or profile-comm\n");
        usage();
        return false;
    }

    if (!trace_is && !trace_elf) {
        log("Neither instruction stream or ELF tracing is enabled. Exiting.\n");
        usage();
        return false;
    }

    if (trace_is && flat_map) {
        log("Instruction stream tracing is incompatible with flat mapping\n");
        usage();
        return false;
    }

    if ((bbv_gen_log && !checkpoint_tag) || (checkpoint_tag && !bbv_gen_log)) {
        log("Error: Both checkpoint-tag= and generation-log= must be specified if either is.\n");
        usage();
        return false;
    }
    if (bbv_gen_log && (profile_el1 || profile_comm || profiling_pids.size())) {
        log("Error: None of profile-el1=, profile-comm=, or profile-pid= can be used in conjunction with "
            "generation-log= \n");
        usage();
        return false;
    }
    if (bbv_gen_log && force_stop_trigger && (stop_trigger != INSTRUCTION || trigger_stop_with_instructions_set)) {
        log("Error: generation-log= handles instruction stop triggers for itself, so they may not be set on the "
            "command-line\n");
        usage();
        return false;
    }
    if (bbv_gen_log && (start_trigger != INSTRUCTION || !trigger_start_after_instructions_set)) {
        log("Error: generation-log= expects you to set up the trigger_start_after_instructions for it\n");
        usage();
        return false;
    }
    // If someone requests multiple traces with an instruction stop trigger,
    // but doesn't specify the periodic instruction start trigger, assume that
    // they want the subsequent traces to begin approximately when the previous
    // trace ends
    if (trigger_stop_with_instructions_set && max_traces > 1 && !trigger_periodically_after_instructions_set) {
        trigger_periodically_after_instructions = trigger_stop_after_measurement_instructions;
        trigger_periodically_after_instructions_set = true;
    }

    return true;
}

/* Called each time the EL or CONTEXTIDR change; updates whether the plugin
 * should be counting instructions to trace in the current context */
void update_watching(uint64_t el, uint64_t pid, ContextUpdateType update_type)
{
    if (debug_mode) {
        if (el != current_el) {
            log("EL transition from %u to %" PRIu64 " (%" PRIu64 " total, %" PRIu64 " watched instructions)\n",
                current_el,
                el,
                total_instruction_count,
                watched_instructions);
        }
        if (pid != current_pid) {
            log("PID change from %" PRIu64 " to %" PRIu64 " (%" PRIu64 " total, %" PRIu64 " watched instructions)\n",
                current_pid,
                pid,
                total_instruction_count,
                watched_instructions);
            current_pid = pid;
        }
    }
    watching = ((profile_all_pids || profiling_pids.find(pid) != profiling_pids.end())
                && (el == 0 || (el == 1 && profile_el1 && !ignoring_async)));
    if (el != current_el) {
        current_el = el;
        new_el = el;
        el_update_type = update_type;

        if (skip_async) {
            if (update_type == ContextUpdateType::ASYNC_EXC) {
                if (debug_mode) {
                    if (ignoring_async == true) {
                        log("Async occurred while already ignoring async. Trace may resume prematurely.\n");
                    } else {
                        log("Skipping instructions after async until next context update.\n");
                    }
                }

                ignoring_async = true;
            } else {
                if (debug_mode && ignoring_async) {
                    log("Resuming instruction tracing after context update while ignoring async.\n");
                }
                /* Reset ignoring async on any el change in order to ensure
                 * we don't lose track of any unexpected/errant behavior due
                 * to optionally ignoring async handling code */
                resuming_from_async = ignoring_async;
                ignoring_async = false;
            }
        }
    }
}

/* Called to update the sysregs we need to track */
void msr_callback(SysReg sysreg, uint64_t value)
{
    sysreg_write.emplace(sysreg, value);
}
void mrs_callback(SysReg sysreg, uint64_t value)
{
    sysreg_read.emplace(sysreg, value);
}

/* State-change helpers */

/* Called each time any tracer's state changes to enable us to compute the
 * global tracing state from that of the individual tracers (whether we are
 * currently doing detailed instruction tracing, the instruction count of the
 * next state transition, etc.)
 */
static bool tracing_instructions = false;
static bool starting_tracing = false;
static std::optional<uint64_t> next_instrcount_event{std::nullopt}; // compared to traced_instructions
static std::optional<uint64_t> next_pc_stop_event{std::nullopt};    // compared to stop_pcs_counted
void update_tracing_status()
{
    bool new_tracing_instructions = false;
    starting_tracing = false;
    next_instrcount_event.reset();
    next_pc_stop_event.reset();

    for (auto &t : tracers) {
        if (t.stopping_tracing) {
            continue;
        }

        if (t.tracing || t.starting_tracing) {
            new_tracing_instructions = true;
        }
        if (t.starting_tracing) {
            starting_tracing = true;
        }

        std::optional<uint64_t> next_trigger_stop_after_pc_occurrences{std::nullopt};
        std::optional<uint64_t> next_instr_count{std::nullopt};
        if (stop_trigger == PC) {
            next_trigger_stop_after_pc_occurrences = t.initial_trigger_stop_after_pc_occurrences + stop_pcs_counted;
        }
        switch (t.state) {
        case COOLING:
            next_instr_count = t.initial_instruction_count + cooldown_instructions;
            break;
        case MEASURING:
            if (stop_trigger == INSTRUCTION) {
                next_instr_count = t.initial_instruction_count + trigger_stop_after_measurement_instructions;
            }
            break;
        case WARMING_ONLINE:
            if (start_measurement_trigger == INSTRUCTION) {
                next_instr_count = t.initial_instruction_count + trigger_measurement_after_online_instructions;
            }
            break;
        case WARMING_OFFLINE:
            if (start_online_warmup_trigger == INSTRUCTION) {
                next_instr_count = t.initial_instruction_count + trigger_online_after_offline_instructions;
            }
            break;
        default:
            log("Invalid tracing_state for %s in update_tracing_status()\n", t.id.c_str());
            exit(0);
        }

        if (next_instr_count.has_value()) {
            if (!next_instrcount_event.has_value() || next_instr_count.value() < next_instrcount_event.value()) {
                next_instrcount_event = next_instr_count.value();
            }
        }
        if (next_trigger_stop_after_pc_occurrences.has_value()) {
            if (!next_pc_stop_event.has_value()
                || next_trigger_stop_after_pc_occurrences.value() < next_pc_stop_event.value())
            {
                next_pc_stop_event = next_trigger_stop_after_pc_occurrences.value();
            }
        }
    }

    // Check if we've completed all tracers. If we have, exit QEMU.
    if (trace_number >= max_traces && tracers.size() == 0) {
        qemu_plugin_request_exit();
    }

    if (new_tracing_instructions != tracing_instructions) {
        tracing_instructions = new_tracing_instructions;
        if (tracing_instructions)
            allocate_state_accessor();
        last_instruction.reset();
        new_el.reset();
        sysreg_write.reset();
        sysreg_read.reset();
    }

    // Note: context->set_sysreg_callbacks calls qemu_plugin_reset(). A call to
    // qemu_plugin_reset here was removed to avoid duplication of this
    // expensive operation, but if the call to set_sysreg_callbacks is removed,
    // it must be added back here.
    if (tracing_instructions) {
        context->set_sysreg_callbacks(&msr_callback, &mrs_callback);
    } else {
        context->set_sysreg_callbacks(nullptr, nullptr);
    }
}

/*
 * handle_tracing_transition() is called to determine which tracers need to
 * change state (if any) when the rest of the code detects that we have crossed
 * a threshold or encountered an event which potentially indicates a tracer
 * state transition is needed.
 */
void handle_tracing_transition()
{
    for (auto &t : tracers) {
        tracing_state next_state = t.state;

        if (t.starting_tracing || t.stopping_tracing)
            continue;

        // Determine whether a state transition will occur for this tracer
        if (t.state == COOLING && traced_instructions > (t.initial_instruction_count + cooldown_instructions)) {
            next_state = STOPPING;
        } else if (hint_semihosting_stop_trigger_seen) {
            if (t.state != MEASURING) {
                logf(
                    "Warning: HINT or SEMIHOSTING stop trigger observed before measurement region was begun for {}. "
                    "State = {}",
                    t.id,
                    static_cast<unsigned>(t.state));
            }
            next_state = COOLING;
            logf("COOLING {} ({} total, {} watched instructions)", t.id, total_instruction_count, watched_instructions);
        } else if (stop_trigger == PC && next_pc_stop_event.has_value()
                   && t.initial_trigger_stop_after_pc_occurrences >= next_pc_stop_event.value())
        {
            if (t.state != MEASURING) {
                logf("Warning: PC stop count matched before tracer was in {}.", t.id);
            }
            next_state = COOLING;
            logf("COOLING {} ({} total, {} watched instructions)", t.id, total_instruction_count, watched_instructions);
        } else if (stop_trigger == INSTRUCTION && t.state == MEASURING
                   && traced_instructions > (t.initial_instruction_count + trigger_stop_after_measurement_instructions))
        {
            next_state = COOLING;
            logf("COOLING {} ({} total, {} watched instructions)", t.id, total_instruction_count, watched_instructions);
        } else if (start_measurement_trigger == INSTRUCTION && t.state == WARMING_ONLINE
                   && traced_instructions
                          > (t.initial_instruction_count + trigger_measurement_after_online_instructions))
        {
            next_state = MEASURING;
            logf("MEASURING for {} ({} total, {} watched instructions)",
                 t.id,
                 total_instruction_count,
                 watched_instructions);
        } else if (start_measurement_trigger == HINT && t.state == WARMING_ONLINE && hint_start_measure_seen) {
            next_state = MEASURING;
            logf("MEASURING for {} ({} total, {} watched instructions)",
                 t.id,
                 total_instruction_count,
                 watched_instructions);
        } else if (start_online_warmup_trigger == INSTRUCTION && t.state == WARMING_OFFLINE
                   && traced_instructions > (t.initial_instruction_count + trigger_online_after_offline_instructions))
        {
            next_state = WARMING_ONLINE;
            logf("WARMING_ONLINE for {} ({} total, {} watched instructions)",
                 t.id,
                 total_instruction_count,
                 watched_instructions);
        } else if (start_online_warmup_trigger == HINT && t.state == WARMING_OFFLINE && hint_start_online_warmup_seen) {
            next_state = WARMING_ONLINE;
            logf("WARMING_ONLINE for {} ({} total, {} watched instructions)",
                 t.id,
                 total_instruction_count,
                 watched_instructions);
        } else {
            continue;
        }

        if (t.state == MEASURING && (MEASURING & t.trace_states)) {
            // If the tracer was already measuring call stop_measurement()
            if (!t.tracer->stop_measurement()) {
                log("Failed to stop measurement when transitioning to COOLING for %s.\n", t.id.c_str());
                t.tracer->abort_tracing();
                t.tracing = false;
                t.stopping_tracing = true;
                continue;
            }
        }

        if (!(t.state & t.trace_states) && (next_state & t.trace_states)) {
            // If the tracer is only beginning to trace, set starting_tracing so
            // that tracer_start_tracing can be called for it before the next
            // instruction
            t.starting_tracing = true;
            t.starting_tracing_at = total_instruction_count;
        } else if ((next_state == STOPPING) || ((t.state & t.trace_states) && !(next_state & t.trace_states))) {
            // If the tracer was previously tracing, but is no longer, remove it
            log("Stopping tracing for %s (%" PRIu64 " total, %" PRIu64 " watched instructions)\n",
                t.id.c_str(),
                total_instruction_count,
                watched_instructions);
            if (!t.tracer->stop_tracing(sve_observed)) {
                log("Failed to stop tracing when transitioning for %s.\n", t.id.c_str());
                t.tracer->abort_tracing();
            }
            t.tracing = false;
            t.stopping_tracing = true;
        } else if (next_state == MEASURING) {
            // If the tracer was already tracing and will still be tracing in
            // the MEASURING phase, start measurement here. Calling
            // start_measurement() will be handled in tracer_start_tracing() if
            // the tracer is just beginning to trace.
            if (!t.tracer->start_measurement()) {
                log("Failed to start measurement when transitioning to MEASURING for %s.\n", t.id.c_str());
                t.tracer->abort_tracing();
                t.tracing = false;
                t.stopping_tracing = true;
                continue;
            }
        }

        t.state = next_state;
        t.initial_instruction_count = traced_instructions;
    }

    // avoid re-triggering transitions
    hint_start_online_warmup_seen = false;
    hint_start_measure_seen = false;
    hint_semihosting_stop_trigger_seen = false;

    // Remove any stopped tracers from the vector
    auto tmp_end =
        std::remove_if(tracers.begin(), tracers.end(), [](const TracerInfo &t) { return t.stopping_tracing; });
    tracers.erase(tmp_end, tracers.end());

    // Update the global tracing flags
    update_tracing_status();
}

void start_tracing()
{
    if (trace_number >= max_traces) {
        return;
    }
    sve_observed = sve_mode; // reset unless always grabbing sve

    // Accounting for periodic instruction-based start triggers
    last_trace_watched_instructions = watched_instructions;

    // Zero padding the trace_number so files are easier to sort.
    char buf0[32], buf1[32];
    sprintf(buf0, "%lu", max_traces - 1);
    sprintf(buf1, "{}_{:#0%lud}", strlen(buf0));
    std::string trace_filename = fmt::format(fmt::runtime(buf1), trace_filename_base, trace_number);

    log("Starting trace %s in WARMING_OFFLINE (%" PRIu64 " total, %" PRIu64 " watched instructions)\n",
        trace_filename.c_str(),
        total_instruction_count,
        watched_instructions);

    /*
     * If no trigger is set to begin online warmup, it means the user accepted
     * the default of 0 instructions of offline warmup, so don't allocate
     * memory or branch stream tracers.
     */
    if (trigger_set(start_online_warmup_trigger,
                    trigger_online_warmup_with_instructions_set && (trigger_online_after_offline_instructions > 0)))
    {
        struct TracerInfo ms_tinfo, bs_tinfo;

        ms_tinfo.id = trace_filename;
        ms_tinfo.type = tracer::Tracer::Type::MS;
        ms_tinfo.tracer = nullptr;
        ms_tinfo.trace_states = WARMING_OFFLINE;
        ms_tinfo.starting_tracing = true;
        ms_tinfo.starting_tracing_at = total_instruction_count;
        ms_tinfo.tracing = false;
        ms_tinfo.stopping_tracing = false;
        ms_tinfo.state = WARMING_OFFLINE;
        ms_tinfo.initial_instruction_count = traced_instructions;
        ms_tinfo.initial_trigger_stop_after_pc_occurrences = stop_pcs_counted;
        ms_tinfo.sve_mode = sve_mode;

        bs_tinfo.id = trace_filename;
        bs_tinfo.type = tracer::Tracer::Type::BS;
        bs_tinfo.tracer = nullptr;
        bs_tinfo.trace_states = WARMING_OFFLINE;
        bs_tinfo.starting_tracing = true;
        bs_tinfo.starting_tracing_at = total_instruction_count;
        bs_tinfo.tracing = false;
        bs_tinfo.stopping_tracing = false;
        bs_tinfo.state = WARMING_OFFLINE;
        bs_tinfo.initial_instruction_count = traced_instructions;
        bs_tinfo.initial_trigger_stop_after_pc_occurrences = stop_pcs_counted;
        bs_tinfo.sve_mode = sve_mode;

        tracers.push_back(std::move(ms_tinfo));
        tracers.push_back(std::move(bs_tinfo));
    }
    if (trace_is) {
        struct TracerInfo tinfo;
        tinfo.id = trace_filename;
        tinfo.type = tracer::Tracer::Type::IS;
        tinfo.tracer = nullptr;
        tinfo.trace_states = (WARMING_ONLINE | MEASURING | COOLING);
        tinfo.starting_tracing = false;
        tinfo.tracing = false;
        tinfo.stopping_tracing = false;
        tinfo.state = WARMING_OFFLINE;
        tinfo.initial_instruction_count = traced_instructions;
        tinfo.initial_trigger_stop_after_pc_occurrences = stop_pcs_counted;
        tinfo.sve_mode = sve_mode;
        tracers.push_back(std::move(tinfo));
    }
    if (trace_elf) {
        struct TracerInfo tinfo;

        tinfo.id = trace_filename;
        tinfo.type = tracer::Tracer::Type::ELF;
        tinfo.tracer = nullptr;
        tinfo.trace_states = (WARMING_ONLINE | MEASURING | COOLING);
        tinfo.starting_tracing = false;
        tinfo.tracing = false;
        tinfo.stopping_tracing = false;
        tinfo.state = WARMING_OFFLINE;
        tinfo.initial_instruction_count = traced_instructions;
        tinfo.initial_trigger_stop_after_pc_occurrences = stop_pcs_counted;
        tinfo.sve_mode = sve_mode;
        tracers.push_back(std::move(tinfo));
    }

    trace_number++;
    update_tracing_status();
}

void tracer_start_tracing(struct TracerInfo &tracer)
{
    logf("Starting tracing {} using {}Tracer ({} total, {} watched instructions)",
         tracer.id,
         tracer.type_str(),
         total_instruction_count,
         watched_instructions);
    try {
        tracer.tracer = tracer::Tracer::make_tracer(tracer.type,
                                                    tracer.id,
                                                    stateaccessor,
                                                    flat_map,
                                                    elf_entry_point,
                                                    debug_mode,
                                                    record_destination_registers,
                                                    sve_mode,
                                                    dcache_filter_size_bytes,
                                                    icache_filter_size_bytes,
                                                    elf_section_span);
        tracer.starting_tracing = false;
        tracer.tracing = true;
        tracer.tracer->record_initial_state();
    } catch (std::runtime_error &e) {
        log("Failed to start tracing %s with exception %s\n", tracer.id.c_str(), e.what());
        tracer.stopping_tracing = true;
        handle_tracing_transition();
        return;
    }

    // If this tracer is beginning tracing for the first time in the MEASURING
    // state, it will not have start_measurement() called on it if we don't do
    // it here
    if (tracer.state == MEASURING) {
        if (!tracer.tracer->start_measurement()) {
            log("Failed to start measurement in tracer_start_tracing() for %s.\n", tracer.id.c_str());
            tracer.tracer->abort_tracing();
            tracer.tracing = false;
            tracer.stopping_tracing = true;
        }
    }

    handle_tracing_transition();
}

void stop_plugin()
{
    if (debug_mode) {
        log("stop_plugin() called (%" PRIu64 " total, %" PRIu64 " watched instructions)\n",
            total_instruction_count,
            watched_instructions);
    }
    // Finalize and clean up tracers
    for (auto &t : tracers) {
        if (t.tracing) {
            if (t.state == MEASURING) {
                log("Stopping measurement for %s at plugin stop-time.\n", t.id.c_str());
                if (!t.tracer->stop_measurement()) {
                    log("Failed to stop measurement in stop_plugin() for %s.\n", t.id.c_str());
                    t.tracer->abort_tracing();
                    continue;
                }
            }
            log("Stopping tracing for %s at plugin stop-time.\n", t.id.c_str());
            if (!t.tracer->stop_tracing(sve_observed)) {
                log("Failed to stop tracing in stop_plugin() for %s.\n", t.id.c_str());
                t.tracer->abort_tracing();
                continue;
            }
        }
    }
    tracers.clear();

    qemu_plugin_request_exit();
}

/* See if the current perf semihosting call is a new process being executed
 * (COMM), and set the pid that we're profiling if it is and the command
 * matches the one we're looking for */
uint64_t perf_semihosting_callback(uint16_t svc, unsigned int cpu_index)
{
    struct perf_event_header header;
    uint64_t x0;
    uint8_t *ptr;

    if (!qemu_plugin_read_reg64(&x0, 0)) {
        log("Failed to read x0 for perf semihosting call\n");
        exit(0);
    }

    if (qemu_plugin_read_memory(x0, (uint8_t *)&header, sizeof(header))) {
        log("Failed to read semihosting perf_event_header\n");
        exit(0);
    }

    /* Ignore the other event types (for now) */
    if (header.type != PERF_RECORD_COMM && header.type != PERF_RECORD_EXIT && header.type != PERF_RECORD_FORK
        && header.type != PERF_RECORD_MMAP2)
        goto out;

    ptr = (uint8_t *)malloc(header.size);
    if (!ptr) {
        log("Failed to allocate memory for semihosting perf event body\n");
        exit(0);
    }

    if (qemu_plugin_read_memory(x0, ptr, header.size)) {
        log("Failed to read semihosting perf event body\n");
        exit(0);
    }

    if (header.type == PERF_RECORD_COMM) {
        struct perf_comm_event *evt = (struct perf_comm_event *)ptr;

        if (debug_mode) {
            log("PID %" PRIu32 " spawned with command name: \"%s\" (%" PRIu64 " total, %" PRIu64
                " watched instructions)\n",
                evt->tid,
                evt->comm,
                total_instruction_count,
                watched_instructions);
        }
        if (profile_comm && profiling_pids.size() == 0 && perf_comm_matches(evt, profile_comm)) {
            profiling_pids.emplace(evt->tid);

            log("Profiling PID %" PRIu32 " with command name: \"%s\"\n", evt->tid, evt->comm);
            waiting_for_profile_comm = false;
        }
    } else if (header.type == PERF_RECORD_EXIT) {
        struct perf_task_event *evt = (struct perf_task_event *)ptr;
        if (debug_mode) {
            log("PID %" PRIu32 " exited (%" PRIu64 " total, %" PRIu64 " watched instructions)\n",
                evt->tid,
                total_instruction_count,
                watched_instructions);
        }
        if (profiling_pids.find(evt->tid) != profiling_pids.end()) {
            profiling_pids.erase(evt->tid);
            if (profiling_pids.size() == 0) {
                log("Exiting due to PID %" PRIu32 " called exit() (%" PRIu64 " total, %" PRIu64
                    " watched instructions)\n",
                    evt->tid,
                    total_instruction_count,
                    watched_instructions);
                stop_plugin();
            }
        }
    } else if (profile_children && header.type == PERF_RECORD_FORK) {
        struct perf_task_event *evt = (struct perf_task_event *)ptr;
        if (debug_mode) {
            log("PID %" PRIu32 " forked from %" PRIu32 " (%" PRIu64 " total, %" PRIu64 " watched instructions)\n",
                evt->tid,
                evt->ptid,
                total_instruction_count,
                watched_instructions);
        }
        if (profiling_pids.find(evt->ptid) != profiling_pids.end()) {
            profiling_pids.emplace(evt->tid);
            log("Profiling newly-forked PID %" PRIu32 " (child of %" PRIu32 ")\n", evt->tid, evt->ptid);
        }
    } else if (debug_mode && header.type == PERF_RECORD_MMAP2) {
        struct perf_mmap2_event *evt = (struct perf_mmap2_event *)ptr;
        log("PID %" PRIu32 " mmapped %s at address 0x%" PRIx64 ", len: %" PRIu64 ", pgoff: %" PRIu64 " (%" PRIu64
            " total, %" PRIu64 " watched instructions)\n",
            evt->tid,
            evt->filename,
            evt->addr,
            evt->len,
            evt->pgoff,
            total_instruction_count,
            watched_instructions);
    }

    free(ptr);

out:
    /* The custom semihosting implementation updates x0 with whatever we return
     * here - leave x0 unchanged. */
    return x0;
}

uint64_t trace_semihosting_callback(uint16_t svc, unsigned int cpu_index)
{
    uint64_t x0;
    if (!qemu_plugin_read_reg64(&x0, 0)) {
        log("Failed to read x0 for trace semihosting call\n");
        exit(0);
    }

    switch (x0) {
    case TRACE_SEMIHOSTING_START:
        if (start_trigger == SEMIHOSTING) {
            log("Starting trace upon observing semihosting call: `hlt #0x%x` with x0 = %u (%" PRIu64 " total, %" PRIu64
                " watched instructions)\n",
                TRACE_SEMIHOSTING_NUM,
                TRACE_SEMIHOSTING_START,
                total_instruction_count,
                watched_instructions);
            start_tracing();
        }
        break;
    case TRACE_SEMIHOSTING_STOP:
        if (stop_trigger == SEMIHOSTING) {
            log("Stopping measurement section of trace upon observing semihosting call: `hlt #0x%x` with x0 = %u "
                "(%" PRIu64 " total, %" PRIu64 " watched instructions)\n",
                TRACE_SEMIHOSTING_NUM,
                TRACE_SEMIHOSTING_STOP,
                total_instruction_count,
                watched_instructions);
            hint_semihosting_stop_trigger_seen = true;
            handle_tracing_transition();
        }
        break;
    }

    /* The custom semihosting implementation updates x0 with whatever we return
     * here - leave x0 unchanged. */
    return x0;
}

static void record_src_gpr(uint8_t r, bool use_xz)
{
    uint64_t val;
    if (r == 31 && use_xz) {
        val = 0; // xzr
    } else if (!qemu_plugin_read_reg64(&val, r)) {
        log("Failed to read value from source GP register %" PRIu32 "\n", r);
        exit(0);
    }
    last_instruction.value().source_regs.emplace(r, val);
}

static void record_src_pred(uint8_t r)
{
    ampere::archx::preg_t pval = stateaccessor->read_pred_register(r);
    last_instruction.value().source_pred_regs.emplace(r, pval.p);
}

static void record_src_vfp(uint8_t v, unsigned first_vfp_regidx)
{
    std::array<uint8_t, ampere::archx::MAXVL> value;
    GByteArray *regval;
    unsigned read_bytes;

    regval = g_byte_array_new();

    // probably should route this through stateaccessor instead
    read_bytes = qemu_plugin_read_register(regval, v + first_vfp_regidx);
    assert(read_bytes == regval->len);
    if (read_bytes != regval->len || regval->len < ampere::archx::MAXVL) {
        // Note, we can expect to read up to 256 bytes here because
        // QEMU stores them using the maximum bit width of SVE
        // internally, 2048 bits.
        log("Failed to read DIV value from source VFP register %" PRIu32 "\n", v);
        g_byte_array_free(regval, true);
        exit(0);
    }
    // TODO We ignore the higher bits of these registers, assuming SVE
    // is not implemented. We should likely make sure that is true or
    // store more bits.
    for (size_t i = 0; i < ampere::archx::MAXVL; i++)
        value.data()[i] = regval->data[i];

    g_byte_array_free(regval, true);

    last_instruction.value().source_vfp_regs.emplace(v, value);
}

/* QEMU plugin hooks */

bool is_load_store(tracer::Instruction inst)
{
    archx::InstructionIS is_instr(inst.pc, inst.opcode);
    return is_instr.is_load() || is_instr.is_store();
}

void before_insn_cb(unsigned int cpu_index, void *udata)
{
    uint64_t pc = (uint64_t)udata;
    uint32_t encoding;
    if (qemu_plugin_read_memory(pc, (uint8_t *)&encoding, 4)) {
        log("before_insn_cb failed to read from 0x%" PRIx64 "\n", pc);
    }

    /* Watch for PC-based start trigger */
    if (watching && start_trigger == PC && pc == trigger_start_after_pc) {
        start_pcs_counted++;
        if (start_pcs_counted == trigger_start_after_pc_occurrences) {
            log("Starting trace upon observing PC 0x%" PRIx64 " %lu times (%" PRIu64 " total, %" PRIu64
                " watched instructions)\n",
                trigger_start_after_pc,
                trigger_start_after_pc_occurrences,
                total_instruction_count,
                watched_instructions);
            start_tracing();
        }
    }

    /* Watch for HINT start/stop triggers */
    if (IS_HINT(encoding, TRACE_HINT_NUM)) {
        uint64_t x0;
        if (!qemu_plugin_read_reg64(&x0, 0)) {
            log("Failed to read x0 for hint tracing call\n");
            exit(0);
        }

        if (start_trigger == HINT && x0 == TRACE_HINT_START) {
            log("Starting trace upon observing: `hint #0x%x` with x0 = %u (%" PRIu64 " total, %" PRIu64
                " watched instructions)\n",
                TRACE_HINT_NUM,
                TRACE_HINT_START,
                total_instruction_count,
                watched_instructions);
            start_tracing();
        } else if (tracing_instructions && start_online_warmup_trigger == HINT && x0 == TRACE_HINT_START_ONLINE_WARMUP)
        {
            logf(
                "Starting online warmup upon observing: `hint #{:#x}` with x0 = {} ({} total, {} watched instructions)",
                TRACE_HINT_NUM,
                TRACE_HINT_START_ONLINE_WARMUP,
                total_instruction_count,
                watched_instructions);
            hint_start_online_warmup_seen = true;
            handle_tracing_transition();
        } else if (tracing_instructions && start_measurement_trigger == HINT && x0 == TRACE_HINT_START_MEASURE) {
            logf("Starting measurement upon observing: `hint #{:#x}` with x0 = {} ({} total, {} watched instructions)",
                 TRACE_HINT_NUM,
                 TRACE_HINT_START_MEASURE,
                 total_instruction_count,
                 watched_instructions);
            hint_start_measure_seen = true;
            handle_tracing_transition();
        } else if (tracing_instructions && stop_trigger == HINT && x0 == TRACE_HINT_STOP) {
            log("Stopping measurement section of trace upon observing: `hint #0x%x` with x0 = %u (%" PRIu64
                " total, %" PRIu64 " watched instructions)\n",
                TRACE_HINT_NUM,
                TRACE_HINT_STOP,
                total_instruction_count,
                watched_instructions);
            hint_semihosting_stop_trigger_seen = true;
            handle_tracing_transition();
        }
    }

    if (tracers.size() > 0) {
        if (watching) {
            traced_instructions += 1;

            // Check if we need a state transition based on instruction count or
            // PC-stop-count.
            if (next_instrcount_event.has_value() && traced_instructions >= next_instrcount_event.value()) {
                handle_tracing_transition();
            }
        }
        if (stop_trigger == PC && pc == trigger_stop_after_pc) {
            stop_pcs_counted++;
            if (next_pc_stop_event.has_value() && stop_pcs_counted == next_pc_stop_event.value()) {
                handle_tracing_transition();
            }
        }
    }

    if (tracing_instructions) {
        uint64_t pc_pa;
        bool skip_instruction = false;

        if (!ignoring_async) {
            if (last_instruction) {
                // Check if the last recorded instruction is a load or store. If it
                // is and has not seen any memory operations, mark it as faulting
                if (!last_instruction.value().memops.size() && context->is_exception(pc)
                    && is_load_store(last_instruction.value()))
                {
                    // ESR.EC code for 'Data Abort from a lower Exception level'
                    last_instruction.value().exception_class = 0b100100;
                }

                // The QEMU plugin interface seems to double-call
                // instruction-execution callbacks for some instructions, so detect
                // and skip emitting the first of the pair of duplicate
                // instructions.
                if (last_instruction.value().pc.value() == pc && last_instruction.value().opcode == encoding
                    && !is_branch(encoding))
                {
                    log("Warning: Skipping first of pair of instructions with PC 0x%" PRIx64 " and encoding 0x%" PRIx32
                        "\n",
                        pc,
                        encoding);
                    skip_instruction = true;
                }
            }

            /* Write previous instruction and state changes before clearing them */
            bool error = false;
            for (auto &t : tracers) {
                if (t.tracing) {
                    if (new_el && !resuming_from_async) {
                        if (!t.tracer->el_change((archx::EL)new_el.value(), el_update_type)) {
                            log("el_change() call failed for %s.\n", t.id.c_str());
                            t.tracer->abort_tracing();
                            t.tracing = false;
                            t.stopping_tracing = true;
                            error = true;
                            continue;
                        }
                    }
                    if (t.tracing && last_instruction && !skip_instruction) {
                        if (!t.tracer->instruction_exec(last_instruction.value(), ProgramCounter{pc})) {
                            log("instruction_exec() call failed for %s.\n", t.id.c_str());
                            t.tracer->abort_tracing();
                            t.tracing = false;
                            t.stopping_tracing = true;
                            error = true;
                            continue;
                        }
                    }
                    if (t.tracing && sysreg_write) {
                        auto sysreg = sysreg_write.value();
                        if (!t.tracer->sysreg_write(sysreg.first, sysreg.second)) {
                            log("sysreg_write() call failed for %s.\n", t.id.c_str());
                            t.tracer->abort_tracing();
                            t.tracing = false;
                            t.stopping_tracing = true;
                            error = true;
                            continue;
                        }
                    }
                    if (t.tracing && sysreg_read) {
                        auto sysreg = sysreg_read.value();
                        if (!t.tracer->sysreg_read(sysreg.first, sysreg.second)) {
                            log("sysreg_read() call failed for %s.\n", t.id.c_str());
                            t.tracer->abort_tracing();
                            t.tracing = false;
                            t.stopping_tracing = true;
                            error = true;
                            continue;
                        }
                    }
                }
            }
            if (error) {
                handle_tracing_transition();
            }
        }

        last_instruction.reset();
        new_el.reset();
        sysreg_write.reset();
        sysreg_read.reset();

        if (tracing_instructions) {
            if (!stateaccessor) {
                THROW("stateaccessor not allocated yet.");
            }
            const unsigned first_vfp_regidx = stateaccessor->get_first_vector_reg_index();
            bool qemu_valid = qemu_plugin_virt_to_phys(pc, &pc_pa);
            if (qemu_valid) {
                last_instruction = tracer::Instruction(pc, pc_pa, encoding);
            } else {
                last_instruction = tracer::Instruction(pc, encoding);
            }

            const auto adis_info = adecode(encoding);
            last_instruction.value().adis_info = adis_info;

            if (adis::sve_family(adis_info)) {
                if (!sve_observed) {
                    log("Observed SVE\n");
                }
                sve_observed = true;

                sveagen::handle_sve_memops(stateaccessor, last_instruction.value());
                if (adis::fp_divide(adis_info) || adis::int_divide(adis_info)) {
                    if (adis::divide_reversed(adis_info)) {
                        record_src_vfp(SVE_LOW_REG(encoding), first_vfp_regidx); // zdm denominator
                        record_src_vfp(SVE_2ND_REG(encoding), first_vfp_regidx); // zm numerator
                    } else {
                        record_src_vfp(SVE_2ND_REG(encoding), first_vfp_regidx); // zm denominator
                        record_src_vfp(SVE_LOW_REG(encoding), first_vfp_regidx); // zdm numerator
                    }
                } else if (adis::sqrt(adis_info)) { // zn
                    record_src_vfp(SVE_2ND_REG(encoding), first_vfp_regidx);
                }

                // record governing predicate reg & value for all SVE
                // as it could be used to create NOPS, or shorten
                // latencies
                auto pg = sveagen::governing_predicate(encoding, adis_info);
                if (pg) {
                    record_src_pred(pg.value());
                }
            } else {
                // Add register values for instructions it is handy to have them
                // for (TLBI, cache maintenance, divides, prefetches)
                auto div_type = divide_type(encoding);
                switch (div_type) {
                case DivideType::INT_DIVIDE:
                    record_src_gpr(DIV_RN(encoding), true /*31 = xz*/);
                    record_src_gpr(DIV_RM(encoding), true /*31 = xz*/);
                    break;
                case DivideType::FP_SCALAR_DIVIDE:
                case DivideType::FP_VECTOR_DIVIDE:
                    record_src_vfp(DIV_RM(encoding), first_vfp_regidx);
                    [[fallthrough]];
                case DivideType::FP_SCALAR_SQRT:
                case DivideType::FP_VECTOR_SQRT:
                    record_src_vfp(DIV_RN(encoding), first_vfp_regidx);
                    break;
                case DivideType::NONE:
                    break;
                default:
                    assert(false);
                }
            }

            auto c_maint_type = cache_maint_type(adis_info);
            if (adis::tlbi(adis_info)
                || (c_maint_type != adis::sys_alias_t::INVALID && c_maint_type != adis::sys_alias_t::IC_IALLU
                    && c_maint_type != adis::sys_alias_t::IC_IALLUIS))
            {
                record_src_gpr(MSR_REG(encoding), false /*31 = sp*/);
            }

            if (adis::prefetch(adis_info)) {
                switch (adis_info) {
                case adis::ENC_PRFM_P_ldst_regoff:
                    record_src_gpr(PREF_RM(encoding), false /*31 = sp*/);
                    [[fallthrough]];
                case adis::ENC_PRFM_P_ldst_pos:
                case adis::ENC_PRFUM_P_ldst_unscaled:
                    record_src_gpr(PREF_RN(encoding), false /*31 = sp*/);
                    break;
                case adis::ENC_PRFM_P_loadlit:
                    break;
                default:
                    // If we care about other prefetches we can catch them here.
                    // SVE prefetches and gather prefetches are handled by the sveagen stuff.
                    if (!adis::sve_family(adis_info)) {
                        THROW(
                            "Unhandled non-SVE prefetch: encoding {:08x} {}", encoding, adis::encoding_name(adis_info));
                    }
                    break;
                }
            }
        }
    }
    context->before_insn(cpu_index, pc, encoding);

    if (starting_tracing) {
        for (auto &t : tracers) {
            if (t.starting_tracing && total_instruction_count > t.starting_tracing_at) {
                tracer_start_tracing(t);
            }
        }
        update_tracing_status();
    }
}

static void mem_cb(unsigned int cpu_index, qemu_plugin_meminfo_t meminfo, uint64_t va, void *udata)
{
#ifndef NDEBUG
    uint64_t pc = (uint64_t)udata;
#endif

    if (!tracing_instructions || !last_instruction || !tracers.size())
        return;

    assert(last_instruction.value().pc.value() == pc);

    unsigned size_shift = qemu_plugin_mem_size_shift(meminfo);
    unsigned size = 0x1 << size_shift;
    bool is_store = qemu_plugin_mem_is_store(meminfo);

    struct qemu_plugin_hwaddr *hwaddr = qemu_plugin_get_hwaddr(meminfo, va);
    if (hwaddr) {
        uint64_t pa = qemu_plugin_hwaddr_phys_addr(hwaddr);
        bool is_io = qemu_plugin_hwaddr_is_io(hwaddr);
        auto memop = tracer::MemOp(va, pa, size, is_store, is_io);
        if (size > sizeof(memop.value)) {
            log("Unexpected memory access of > 128 bytes");
            exit(0);
        }
        if (!is_io && qemu_plugin_read_phys_memory(pa, memop.value, size, 0 /* ARMASIdx_NS */)) {
            log("Failed to read memory backing memory access at PA: 0x%" PRIx64 ", size %u, %s\n",
                pa,
                size,
                is_store ? "store" : "load");
            exit(0);
        }
        last_instruction.value().memops.emplace_back(std::move(memop));
    } else {
        last_instruction.value().memops.emplace_back(va, size, is_store);
    }
}

void before_tb_cb(unsigned int cpu_index, void *udata)
{
    uint64_t num_insns = (uint64_t)udata;

    total_instruction_count += num_insns;
    if (watching) {
        watched_instructions += num_insns;

        if (trace_number == 0 && start_trigger == INSTRUCTION
            && watched_instructions >= last_trace_watched_instructions + trigger_start_after_instructions)
        {
            start_tracing();
        } else if (trace_number > 0 && trigger_periodically_after_instructions_set
                   && watched_instructions >= last_trace_watched_instructions + trigger_periodically_after_instructions)
        {
            start_tracing();
        }
    }
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n = qemu_plugin_tb_n_insns(tb);
    size_t i;

    for (i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        uint64_t pc = qemu_plugin_insn_vaddr(insn);
        uint32_t encoding = *((uint32_t *)qemu_plugin_insn_data(insn));

        if (tracers.size() > 0 || context->needs_before_insn(id, pc, encoding)
            || ((start_trigger == HINT || stop_trigger == HINT) && IS_HINT(encoding, TRACE_HINT_NUM))
            || (start_trigger == PC && pc == trigger_start_after_pc)
            || (stop_trigger == PC && pc == trigger_stop_after_pc))
        {
            qemu_plugin_register_vcpu_insn_exec_cb(insn, before_insn_cb, QEMU_PLUGIN_CB_R_REGS, (void *)pc);
        }
        if (tracing_instructions)
            qemu_plugin_register_vcpu_mem_cb(insn, mem_cb, QEMU_PLUGIN_CB_NO_REGS, QEMU_PLUGIN_MEM_RW, (void *)pc);
    }
    qemu_plugin_register_vcpu_tb_exec_cb(tb, before_tb_cb, QEMU_PLUGIN_CB_NO_REGS, (void *)n);
}

bool checkpoint_context_init()
{
    std::ifstream bbv_log_stream(bbv_gen_log);
    json jval;
    try {
        jval = json::parse(bbv_log_stream);
    } catch (json::parse_error &e) {
        log("Error parsing JSON from BBV generation log file: %s\n", e.what());
        return false;
    }

    /* Load profiling PID and whether to profile in EL1 from JSON.
     *
     * Note: These variables MUST be set before context->from_json() is called
     * below, because that code calls the update_profiling() callback, which
     * relies upon the values in profile_el1 and profile_pid to be correct.
     */
    if (!jval.count("settings")) {
        log("Failed to find settings object in JSON file\n");
        return false;
    }
    json settings = jval["settings"];
    if ((!settings.count("profile_pids") && !settings.count("profile_pid")) || !settings.count("profile_el1")
        || !settings.count("bbv_interval"))
    {
        log("Failed to find profiling settings in JSON file\n");
        return false;
    }
    profile_el1 = settings["profile_el1"].get<bool>();

    /* Discover and load up all of the PIDs that were profiled */
    if (settings.count("profile_pids")) {
        auto json_pids = settings["profile_pids"];
        for (auto &pid : json_pids)
            profiling_pids.emplace(num_string(pid));
    } else {
        profiling_pids.emplace(num_string(settings["profile_pid"]));
    }

    if (force_stop_trigger) {
        trigger_stop_after_measurement_instructions = settings["bbv_interval"];
        assert(stop_trigger == INSTRUCTION);
    }

    /* Load context from checkpoint generation log */
    if (!jval.count("checkpoints") || !jval["checkpoints"].is_array()) {
        log("BBV generation log JSON file doesn't contain a 'checkpoints' array\n");
        return false;
    }

    bool context_initialized = false;

    for (auto &ckpt : jval["checkpoints"]) {
        if (!ckpt.count("context") || !ckpt.count("tag")) {
            log("Improperly formatted JSON checkpoint object\n");
            break;
        }

        if (ckpt["tag"] == checkpoint_tag) {
            if (!context->from_json(ckpt["context"])) {
                log("Failed to import context from JSON\n");
                break;
            }
            context_initialized = true;
            break;
        }
    }

    if (!context_initialized) {
        log("Failed to find checkpoint to initialize context from JSON BBV log file\n");
    }
    return context_initialized;
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    if (profile_comm && waiting_for_profile_comm) {
        logf("Failed to find profile command {}. Trace may be unsatisfactory", profile_comm);
    }
    stop_plugin();

    delete context;
    context = nullptr;

    if (stateaccessor) {
        delete stateaccessor;
        stateaccessor = nullptr;
    }

    profiling_pids.clear();
}

void plugin_reset_callback(qemu_plugin_id_t id)
{
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, nullptr);
}

QEMU_PLUGIN_EXPORT
int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info, int argc, char **argv)
{
    // Save ID at initialization-time, some later calls need it;
    plugin_id = id;

    if (!parse_args(argc, argv))
        return -1;

    if (qemu_plugin_semihosting_register(PERF_SEMIHOSTING_NUM, perf_semihosting_callback)) {
        log("Error registering perf semihosting callback\n");
        return -1;
    }
    if (qemu_plugin_semihosting_register(TRACE_SEMIHOSTING_NUM, trace_semihosting_callback)) {
        log("Error registering trace semihosting callback\n");
        return -1;
    }

    context = new Context(id, &plugin_reset_callback, &update_watching, current_el, 0);

    /* If we're collecting a simpoint interval starting from a checkpoint,
     * initialize our internal context to match that checkpoint's */
    if (bbv_gen_log) {
        if (!checkpoint_context_init())
            return -1;
    }

    /* Tell the user what we're doing */
    log("Saving up to %" PRIu64 " ", max_traces);
    if (trace_is && trace_elf)
        log("instruction streams and ELFs ");
    else if (trace_is)
        log("instruction streams ");
    else if (trace_elf)
        log("ELFs ");
    if (start_online_warmup_trigger == INSTRUCTION) {
        if (trigger_online_after_offline_instructions) {
            log("with %lu instructions of offline branch and memory stream warmup ",
                trigger_online_after_offline_instructions);
        } else {
            log("with no offline warmup ");
        }
    } else if (start_online_warmup_trigger == HINT) {
        log("with offline warmup from start of trace until `hint #0x%x` with x0 = %u ",
            TRACE_HINT_NUM,
            TRACE_HINT_START_ONLINE_WARMUP);
    }
    if (profile_all_pids) {
        log("in all PIDs");
    } else {
        log("in ");
        if (profile_children)
            log("process tree of ");
        if (profiling_pids.size())
            log("PID %" PRId64, *profiling_pids.begin());
        else
            log("first process with command name \"%s\"", profile_comm);
    }
    log(" with start trigger after:\n");
    switch (start_trigger) {
    case INSTRUCTION:
        log("    Waiting %" PRIu64 " instructions after first EL0 ", trigger_start_after_instructions);
        if (profile_el1)
            log("or EL1 ");
        log("instruction in process\n");
        break;
    case SEMIHOSTING:
        log("    Semihosting call `hlt #0x%x` with x0 = %u\n", TRACE_SEMIHOSTING_NUM, TRACE_SEMIHOSTING_START);
        break;
    case HINT:
        log("    Hint call `hint #0x%x` with x0 = %u\n", TRACE_HINT_NUM, TRACE_HINT_START);
        break;
    case PC:
        log("    PC 0x%" PRIx64 " is executed %lu times\n", trigger_start_after_pc, trigger_start_after_pc_occurrences);
        break;
    }
    log("and stop trigger after:\n");
    switch (stop_trigger) {
    case INSTRUCTION:
        log("    %" PRIu64 " instructions after start trigger\n", trigger_stop_after_measurement_instructions);
        break;
    case SEMIHOSTING:
        log("    Semihosting call `hlt #0x%x` with x0 = %u\n", TRACE_SEMIHOSTING_NUM, TRACE_SEMIHOSTING_STOP);
        break;
    case HINT:
        log("    Hint call `hint #0x%x` with x0 = %u\n", TRACE_HINT_NUM, TRACE_HINT_STOP);
        break;
    case PC:
        log("    PC 0x%" PRIx64 " is executed %lu times after start trigger\n",
            trigger_stop_after_pc,
            trigger_stop_after_pc_occurrences);
        break;
    }
    if (trace_elf) {
        log("ELF files will have entry points at 0x%" PRIx64 " and will %sbe flat mapped\n",
            elf_entry_point,
            flat_map ? "" : "not ");
    }

    // Call the callback here manually to originally register the hooks we want
    plugin_reset_callback(id);

    // Setup furiosa decoder (the branch steam tracer uses it to determine
    // which type of a branch it is looking at)
    archx::Instruction::set_decoders({"connor"});
    return 0;
}
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
