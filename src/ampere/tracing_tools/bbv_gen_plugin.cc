//
// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <glib.h>
#include <unistd.h>
#include <set>

#include <nlohmann/json.hpp>
#include <fmt/format.h>

#include "bbv.h"
#include "common.h"
#include "context.h"
#include "tracing_hints.h"
#include "perf_semihosting.h"
#include "encodings.h"

extern "C" {
#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

using nlohmann::json;
using namespace ampere;
using namespace tracer;

/* The following block of variables are all set from arguments, and should not
 * be otherwise modified */
static const char *bbv_filename;
static const char *bbv_gen_log;
static long unsigned bbv_interval;
static bool profile_el1;
static bool profile_children;
static char *profile_comm;
static bool start_tracing_after_hint;     // false;
static bool stop_tracing_after_hint;      // false;
std::set<uint64_t> profiling_pids;        // exception to the rule, can be modified by perf semihosting
std::set<uint64_t> total_prof_pids;       // holds all the pids
static long unsigned checkpoint_interval; // 0 = disabled by default
static long unsigned heartbeat_interval;  // 0 = disabled by default
static bool debug_mode = false;
static bool mav_collection = false;
static const char *mav_filename;
static long unsigned mav_interval;
static uint64_t mav_blocksize;

/* Variables/objects directly related to BBV profiling */
static uint64_t profiling_instrcount = 0;
static uint64_t start_hint_seen = false;
static bool profiling = false;
static BBV *bbv;
static Context *context;
static FILE *bbv_gen_logfile;
static uint64_t current_pid;
static uint64_t current_el;

struct id_count
{
    uint64_t id : 32;
    uint64_t count : 32;
};
typedef union
{
    struct id_count s;
    void *ptr;
} id_count_t;
#define NOT_A_BRANCH_ID UINT32_MAX

std::vector<uint64_t> id_to_addr;
std::unordered_map<uint64_t, uint32_t> addr_to_id;

/* Variables/objects directly related to MAV profiling */
static FILE *mav_file = nullptr;
static uint64_t mav_inst_count = 0;
static uint64_t page_mask;
static enum qemu_plugin_mem_rw rw = QEMU_PLUGIN_MEM_RW;
static bool track_only_io;

static GMutex lock;
static GHashTable *pages;

typedef struct
{
    uint64_t page_address;
    int cpu_read;
    int cpu_write;
    uint64_t reads;
    uint64_t writes;
} PageCounters;

void usage()
{
    log("\n" __FILE__
        " Usage:\n"
        "  bbv-filename=FILE\n"
        "        Path to where the basic block vectors should be stored\n"
        "        (default: vectors.bbv)\n"
        "  generation-log=JSON_FILE\n"
        "        Path to write the BBV generation metadata to\n"
        "        (default: bbv_generation.json)\n"
        "  bbv-interval=IVAL\n"
        "        Number of instructions to include in each interval\n"
        "        (default %lu)\n"
        "  profile-el1=[true|false]\n"
        "        Profile EL1 in addition to EL0 (default: false)\n"
        "  profile-comm=COMM\n"
        "        Restrict BBV profiling to the first PID found with this\n"
        "        command name\n"
        "  profile-pid=PID\n"
        "        Restrict BBV profiling to the given PID\n"
        "  profile-children=[true|false]\n"
        "        For profile-comm= or profile-pid=, profile any child\n"
        "        processes in addition to the parent. This is recursive -\n"
        "        all descendent processes will be profiled. (default:false)\n"
        "  heartbeat-interval=IVAL\n"
        "        Print heartbeat to stdout every IVAL profiled instructions\n"
        "        (default is 0, disabling this feature)\n"
        "  start-tracing-after-hint=[true|false]\n"
        "        Start tracing when `hint #0x55` is seen with x0 = 1 (default: false)\n"
        "  stop-tracing-after-hint=[true|false]\n"
        "        Stop tracing when `hint #0x55` is seen with x0 = 2 (default: false)\n"
        "  checkpoint-interval=IVAL\n"
        "        Number of instructions to wait between checkpoints\n"
        "        (default is 0, disabling this feature)\n"
        "  debug=[true|false]\n"
        "        Enable debug prints for OS forks, context switches,\n"
        "        and other interesting events. (default: false)\n"
        "  trace-mav=[true|false]\n"
        "        Enable memory access vector (MAV) collection. (default: false)\n"
        "  mav-filename=FILE\n"
        "        Path to where the memory access vectors should be stored\n"
        "        (default: vectors.mav)\n"
        "  mav-blocksize=IVAL\n"
        "        Size of memory block to track. Expects power of 2. (default: 4096)\n"
        "  mav-interval=IVAL\n"
        "        Number of instructions to include in each MAV interval\n"
        "        (default = %lu)\n",
        bbv_interval,
        mav_interval);
}

void reset()
{
    bbv_filename = "vectors.bbv";
    bbv_gen_log = "bbv_generation.json";
    bbv_interval = 100000000;
    mav_interval = 100000000;
    mav_blocksize = 4096;
    profile_el1 = false;
    profile_comm = nullptr;
    profile_children = false;
    checkpoint_interval = 0;
    heartbeat_interval = 0;
    mav_filename = "vectors.mav";
    track_only_io = false;

    start_tracing_after_hint = false;
    stop_tracing_after_hint = false;

    profiling_instrcount = 0;
    mav_inst_count = 0;
    profiling = false;
    start_hint_seen = false;

    profiling_pids.clear();
    total_prof_pids.clear();
    current_pid = 0;
    current_el = 3;

    id_to_addr.clear();
    addr_to_id.clear();
}

bool parse_args(int argc, char **argv)
{
    char *endptr;
    for (int i = 0; i < argc; i++) {
        char *opt = argv[i];
        g_autofree char **tokens = g_strsplit(opt, "=", 2);
        if (g_strcmp0(tokens[0], "bbv-filename") == 0) {
            bbv_filename = strdup(tokens[1]);
            if (!bbv_filename) {
                log("Couldn't allocate memory for bbv_filename\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "generation-log") == 0) {
            bbv_gen_log = strdup(tokens[1]);
            if (!bbv_gen_log) {
                log("Couldn't allocate memory for bbv_gen_log\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "mav-filename") == 0) {
            mav_filename = strdup(tokens[1]);
            if (!mav_filename) {
                log("Couldn't allocate memory for mav-filename\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "bbv-interval") == 0) {
            bbv_interval = strtoul(tokens[1], &endptr, 10);
            if (!bbv_interval && endptr == tokens[1]) {
                log("Error: Couldn't parse argument to profile-pid as a decimal\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "mav-blocksize") == 0) {
            mav_blocksize = strtoul(tokens[1], &endptr, 10);
            if (!mav_blocksize && endptr == tokens[1]) {
                log("Error: Couldn't parse argument to mav-blocksize as a decimal\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "mav-interval") == 0) {
            mav_interval = strtoul(tokens[1], &endptr, 10);
            if (!mav_interval && endptr == tokens[1]) {
                log("Error: Couldn't parse argument to mav-interval as a decimal\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "profile-el1") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &profile_el1)) {
                log("Error: Couldn't parse argument to profile-el1 as a boolean\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "profile-comm") == 0) {
            profile_comm = strdup(tokens[1]);
            if (!profile_comm) {
                log("Error: Couldn't allocate memory for profile_comm\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "profile-pid") == 0) {
            uint64_t profile_pid = strtoul(tokens[1], &endptr, 10);
            if (!profile_pid && endptr == tokens[1]) {
                log("Error: Couldn't parse argument to profile-pid as a decimal\n");
                return false;
            }
            profiling_pids.emplace(profile_pid);
            total_prof_pids.emplace(profile_pid);
        } else if (g_strcmp0(tokens[0], "profile-children") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &profile_children)) {
                log("Error: Couldn't parse argument to profile-children as a boolean\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "checkpoint-interval") == 0) {
            checkpoint_interval = strtoul(tokens[1], &endptr, 10);
            if (!checkpoint_interval && endptr == tokens[1]) {
                log("Error: Couldn't parse argument to checkpoint-interval as a decimal\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "heartbeat-interval") == 0) {
            heartbeat_interval = strtoul(tokens[1], &endptr, 10);
            if (!heartbeat_interval && endptr == tokens[1]) {
                log("Error: Couldn't parse argument to heartbeat-interval as a decimal\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "start-tracing-after-hint") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &start_tracing_after_hint)) {
                log("Error: Couldn't parse argument to start-tracing-after-hint as a boolean\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "stop-tracing-after-hint") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &stop_tracing_after_hint)) {
                log("Error: Couldn't parse argument to stop-tracing-after-hint as a boolean\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "debug") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &debug_mode)) {
                log("Error: Couldn't parse argument to debug= as a boolean\n");
                return false;
            }
        } else if (g_strcmp0(tokens[0], "trace-mav") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &mav_collection)) {
                log("Error: Couldn't parse argument to mav_collection as a boolean\n");
                return false;
            }
        } else {
            log("Error: Unrecognized option: '%s'\n", opt);
            usage();
            return false;
        }
    }

    if ((profiling_pids.size() && profile_comm) || (!profiling_pids.size() && !profile_comm)) {
        log("Error: Specify exactly one of profile-pid or profile-comm\n");
        log("profiling_pids.size() = %lu, profile_comm = \"%s\"\n", profiling_pids.size(), profile_comm);
        if (profiling_pids.size())
            log("profiling_pids[0] is %lu\n", *profiling_pids.begin());

        usage();
        return false;
    }

    return true;
}

/*
 * Write the current settings out as a JSON object. This will be the last time
 * output is written to this file, so also close the top-level JSON object with
 * '}'.
 */
bool write_settings_json()
{
    char *cwd;

    if (fputs("\n],\n\"settings\": ", bbv_gen_logfile) < 0) {
        log("Error starting checkpoint file JSON object\n");
        return false;
    }

    cwd = get_current_dir_name();
    if (!cwd) {
        log("Error querying current working directory");
        return false;
    }

    json settings_json;
    settings_json["bbv_filename"] = std::string(bbv_filename);
    settings_json["bbv_interval"] = bbv_interval;
    settings_json["profile_el1"] = profile_el1;
    settings_json["profile_children"] = profile_children;
    settings_json["profile_pids"] = total_prof_pids;
    settings_json["total_instructions"] = profiling_instrcount;
    settings_json["checkpoint_interval"] = checkpoint_interval;
    settings_json["generation_log"] = std::string(bbv_gen_log);
    settings_json["working_directory"] = std::string(cwd);
    settings_json["mav_collection"] = mav_collection;
    settings_json["mav_filename"] = std::string(mav_filename);
    settings_json["mav_interval"] = mav_interval;
    settings_json["mav_blocksize"] = mav_blocksize;

    free(cwd);

    if (fputs(settings_json.dump(4).c_str(), bbv_gen_logfile) < 0) {
        log("Error writing settings as JSON object\n");
        return false;
    }
    if (fputs("\n}\n", bbv_gen_logfile) < 0) {
        log("Error ending checkpoint file JSON object\n");
        return false;
    }

    return true;
}

/* Called each time the EL or CONTEXTIDR change; updates whether the plugin
 * should be capturing BBVs in the current context */
void update_profiling(uint64_t el, uint64_t pid, ContextUpdateType update_type)
{
    if (debug_mode) {
        if (el != current_el) {
            log("EL transition from %u to %" PRIu64 " (%" PRIu64 " profiled instructions)\n",
                current_el,
                el,
                profiling_instrcount);
        }
        if (pid != current_pid) {
            log("PID change from %" PRIu64 " to %" PRIu64 " (%" PRIu64 " profiled instructions)\n",
                current_pid,
                pid,
                profiling_instrcount);
            current_pid = pid;
        }
    }
    current_el = el;
    current_pid = pid;
    profiling = (profiling_pids.find(pid) != profiling_pids.end()) && (el == 0 || (el == 1 && profile_el1))
                && (!start_tracing_after_hint || start_hint_seen);
}

void final_ckpt_cb(long unsigned int error, void *cb_arg)
{
    if (error) {
        log("Error saving final checkpoint: %d\n", error);
    } else {
        log("Exiting after saving final checkpoint\n");
    }
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
    if (header.type != PERF_RECORD_COMM && header.type != PERF_RECORD_FORK && header.type != PERF_RECORD_EXIT)
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
            log("PID %" PRIu32 " spawned with command name: \"%s\" (%" PRIu64 " profiled instructions)\n",
                evt->tid,
                evt->comm,
                profiling_instrcount);
        }
        if (profile_comm && profiling_pids.size() == 0 && perf_comm_matches(evt, profile_comm)) {
            profiling_pids.emplace(evt->tid);
            total_prof_pids.emplace(evt->tid);

            log("Profiling PID %" PRIu32 " with command name: \"%s\"\n", evt->tid, evt->comm);
        }

    } else if (header.type == PERF_RECORD_EXIT) {
        struct perf_task_event *evt = (struct perf_task_event *)ptr;

        if (debug_mode) {
            log("PID %" PRIu32 " exited (%" PRIu64 " profiled instructions)\n", evt->tid, profiling_instrcount);
        }
        if (profiling_pids.find(evt->tid) != profiling_pids.end()) {
            profiling_pids.erase(evt->tid);
            if (profiling_pids.size() == 0) {
                log("Saving final checkpoint after PID %" PRIu32 " exited after executing %" PRId64 " instructions\n",
                    evt->tid,
                    profiling_instrcount);
                qemu_plugin_savevm("bbv-final", final_ckpt_cb, nullptr);
            }
        }

    } else if (profile_children && header.type == PERF_RECORD_FORK) {
        struct perf_task_event *evt = (struct perf_task_event *)ptr;

        if (debug_mode) {
            log("PID %" PRIu32 " forked from %" PRIu32 " (%" PRIu64 " profiled instructions)\n",
                evt->tid,
                evt->ptid,
                profiling_instrcount);
        }
        if (profiling_pids.find(evt->ptid) != profiling_pids.end()) {
            profiling_pids.emplace(evt->tid);
            total_prof_pids.emplace(evt->tid);
            log("Profiling newly-forked PID %" PRIu32 " (child of %" PRIu32 ") - total profiled pids = %lu\n",
                evt->tid,
                evt->ptid,
                total_prof_pids.size());
        }
    }

    free(ptr);

out:
    /* The custom semihosting implementation updates x0 with whatever we return
     * here - leave x0 unchanged. */
    return x0;
}

static uint64_t checkpoint_number = 0;
static bool taking_checkpoint = false;
void ckpt_cb(long unsigned int error, void *cb_arg)
{
    json ckpt_json, context_json;
    std::string tag_str((char *)cb_arg);

    if (error) {
        log("Error saving checkpoint: %d\n", error);
        goto out;
    }

    ckpt_json["tag"] = tag_str;
    ckpt_json["instr_count"] = profiling_instrcount;
    context->to_json(context_json);
    ckpt_json["context"] = context_json;

    if (fprintf(bbv_gen_logfile, checkpoint_number ? ",\n%s" : "\n%s", ckpt_json.dump(4).c_str()) < 0) {
        log("Error printing %s,%" PRId64 " to BBV generation logfile\n", (char *)cb_arg, profiling_instrcount);
        goto out;
    }
    log("Printed %s,%" PRId64 " to BBV generation logfile\n", (char *)cb_arg, profiling_instrcount);

    log("Saved checkpoint %" PRId64 "\n", checkpoint_number);

out:
    checkpoint_number++;
    taking_checkpoint = false;
    free(cb_arg); // free the checkpoint name
}

/* Check to see if a checkpoint needs to be taken based on the most recent
 * profiled TB's instruction count, and take one if so */
void check_checkpoints(uint64_t num_insns)
{
    if (checkpoint_interval && (profiling_instrcount > checkpoint_number * checkpoint_interval) && !taking_checkpoint) {
        log("Saving checkpoint %" PRId64 "\n", checkpoint_number);
        char *checkpoint_name;
        int strlen = snprintf(nullptr, 0, "bbv-%" PRId64, checkpoint_number);
        if (strlen < 0) {
            log("Failing to save checkpoint %" PRId64 " because initial snprintf returned %d\n",
                checkpoint_number,
                strlen);
            return;
        }
        checkpoint_name = (char *)malloc(sizeof(char) * (strlen + 1));
        if (snprintf(checkpoint_name, strlen + 1, "bbv-%" PRId64, checkpoint_number) != strlen) {
            log("Failing to save checkpoint %" PRId64 " because second snprintf returned %d\n",
                checkpoint_number,
                strlen);
            free(checkpoint_name);
            return;
        }

        taking_checkpoint = true;
        qemu_plugin_savevm(checkpoint_name, ckpt_cb, checkpoint_name);
    }
}

static auto commas(std::uint64_t v)
{
    auto with_commas = fmt::group_digits(v);
    return with_commas;
}

/* Check to see if the heartbeat has been passed. If so, print out the
 * heartbeat and reset the local counter */
static uint64_t heartbeat_number = 0;
void check_heartbeat(uint64_t num_insns)
{
    if (!heartbeat_interval)
        return;

    uint64_t next_heartbeat = heartbeat_number * heartbeat_interval;
    if (profiling_instrcount > next_heartbeat) {
        auto instr_comma = commas(next_heartbeat);
        auto precise_comma = commas(profiling_instrcount);

        fmt::print("Heartbeat after {} profiled instructions (precisely {})\n", instr_comma, precise_comma);
        heartbeat_number++;
    }
}

void context_before_insn(unsigned int cpu_index, void *udata)
{
    uint64_t pc = (uint64_t)udata;
    uint32_t encoding;
    if (qemu_plugin_read_memory(pc, (uint8_t *)&encoding, 4)) {
        log("Error: context_before_insn failed to read from 0x%" PRIx64 "\n", pc);
        exit(1);
    }
    context->before_insn(cpu_index, pc, encoding);
}

void tracing_hint(unsigned int cpu_index, void *udata)
{
    uint64_t x0;
    if (!qemu_plugin_read_reg64(&x0, 0)) {
        log("Failed to read x0 for hint tracing call\n");
        exit(0);
    }

    if (start_tracing_after_hint && x0 == TRACE_HINT_START) {
        log("Starting collecting BBV upon observing: `hint #0x%x` with x0 = %u (%" PRIu64 " profiled instructions)\n",
            TRACE_HINT_NUM,
            TRACE_HINT_START,
            profiling_instrcount);
        start_hint_seen = true;
        update_profiling(current_el, current_pid, ContextUpdateType::TRACING);
    } else if (stop_tracing_after_hint && x0 == TRACE_HINT_STOP) {
        log("Stopping BBV collection and saving final checkpoint upon observing: `hint #0x%x` with x0 = %u (%" PRIu64
            " profiled instructions)\n",
            TRACE_HINT_NUM,
            TRACE_HINT_STOP,
            profiling_instrcount);
        qemu_plugin_savevm("bbv-final", final_ckpt_cb, nullptr);
    }
}

static void mav_report_and_reset(void)
{
    g_autoptr(GString) report = g_string_new(nullptr);
    GHashTableIter iter;
    gpointer key, value;
    PageCounters *pc;
    g_hash_table_iter_init(&iter, pages);
    g_string_append_printf(report, "P");
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        pc = (PageCounters *)value;
        if (pc->reads + pc->writes > 0) {
            // g_string_append_printf(report, ":0x%016ld:%ld ", pc->page_address, pc->reads+pc->writes);
            g_string_append_printf(report, ":0x%" PRIx64 ":%ld,%ld ", pc->page_address, pc->reads, pc->writes);
            // g_string_append_printf(report, "0x%" PRIx64 ",%ld,%ld\n", pc->page_address, pc->reads, pc->writes);
        }
        pc->reads = 0;  // Reset reads
        pc->writes = 0; // Reset writes
    }
    g_string_append_printf(report, "\n");

    if (mav_file) {
        fprintf(mav_file, "%s", report->str);
    } else if (mav_collection) {
        fprintf(stderr, "Failed to open MAV file\n");
    }
}

void count_block(unsigned int cpu_index, void *udata)
{
    static uint32_t bb_instrcount = 0;

    id_count_t cnt{.ptr = udata};
    bb_instrcount += cnt.s.count;

    if (cnt.s.id != NOT_A_BRANCH_ID) {
        if (profiling) {
            profiling_instrcount += bb_instrcount;
            mav_inst_count += bb_instrcount;

            if (mav_inst_count >= bbv_interval) {
                mav_report_and_reset();
                mav_inst_count -= bbv_interval;
            }

            uint64_t pc = id_to_addr.at(cnt.s.id);
            bbv->record_bb(pc, bb_instrcount);
            check_checkpoints(bb_instrcount);
            check_heartbeat(bb_instrcount);
        }
        bb_instrcount = 0;
    }
}

// Figure out the ID for this address in id_to_addr
uint32_t get_address_id(uint64_t address)
{
    auto found = addr_to_id.find(address);
    if (found != addr_to_id.end()) {
        return found->second;
    }
    uint32_t new_id = id_to_addr.size();
    addr_to_id[address] = new_id;
    id_to_addr.push_back(address);
    return new_id;
}

static void vcpu_haddr(unsigned int cpu_index, qemu_plugin_meminfo_t meminfo, uint64_t vaddr, void *udata)
{
    struct qemu_plugin_hwaddr *hwaddr = qemu_plugin_get_hwaddr(meminfo, vaddr);
    uint64_t page;
    PageCounters *count;

    /* We only get a hwaddr for system emulation */
    if (track_only_io) {
        if (hwaddr && qemu_plugin_hwaddr_is_io(hwaddr)) {
            page = vaddr;
        } else {
            return;
        }
    } else {
        if (hwaddr && !qemu_plugin_hwaddr_is_io(hwaddr)) {
            page = (uint64_t)qemu_plugin_hwaddr_phys_addr(hwaddr);
        } else {
            page = vaddr;
        }
    }
    page &= ~page_mask;

    if (profiling) {
        g_mutex_lock(&lock);
        count = (PageCounters *)g_hash_table_lookup(pages, GUINT_TO_POINTER(page));

        if (!count) {
            count = g_new0(PageCounters, 1);
            count->page_address = page;
            g_hash_table_insert(pages, GUINT_TO_POINTER(page), (gpointer)count);
        }
        if (qemu_plugin_mem_is_store(meminfo)) {
            count->writes++;
            count->cpu_write |= (1 << cpu_index);
        } else {
            count->reads++;
            count->cpu_read |= (1 << cpu_index);
        }

        g_mutex_unlock(&lock);
    }
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n = qemu_plugin_tb_n_insns(tb);
    size_t i;
    uint32_t count = 0;

    for (i = 0; i < n; i++) {
        ++count;
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        if (mav_collection) {
            qemu_plugin_register_vcpu_mem_cb(insn, vcpu_haddr, QEMU_PLUGIN_CB_NO_REGS, rw, nullptr);
        }
        uint64_t pc = qemu_plugin_insn_vaddr(insn);
        uint32_t encoding = *((uint32_t *)qemu_plugin_insn_data(insn));
        if (context->needs_before_insn(id, pc, encoding)) {
            qemu_plugin_register_vcpu_insn_exec_cb(insn, context_before_insn, QEMU_PLUGIN_CB_R_REGS, (void *)pc);
        }
        if (IS_HINT(encoding, TRACE_HINT_NUM) && (start_tracing_after_hint || stop_tracing_after_hint)) {
            qemu_plugin_register_vcpu_insn_exec_cb(insn, tracing_hint, QEMU_PLUGIN_CB_R_REGS, (void *)pc);
        }
        if (is_branch(encoding) || IS_ERET(encoding) || IS_SVC(encoding) || IS_HVC(encoding) || IS_SMC(encoding)) {
            assert(i == n - 1);
            if (i != n - 1) {
                log("Warning: Branch instruction found not at the end of a TB (%dth instruction out of %d)\n", i, n);
            }

            id_count_t cnt{.s = {.id = get_address_id(pc), .count = count}};
            count = 0;

            qemu_plugin_register_vcpu_insn_exec_cb(insn, count_block, QEMU_PLUGIN_CB_NO_REGS, cnt.ptr);
        } else if (i == n - 1) {
            id_count_t cnt{.s = {.id = NOT_A_BRANCH_ID, .count = count}};

            qemu_plugin_register_vcpu_insn_exec_cb(insn, count_block, QEMU_PLUGIN_CB_NO_REGS, cnt.ptr);
        }
    }
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    if (debug_mode) {
        log("plugin_exit() called (%" PRIu64 " profiled instructions)\n", profiling_instrcount);
    }

    if (!write_settings_json()) {
        log("Error: Failed to write profiling settings via JSON\n");
    }

    if (fclose(bbv_gen_logfile)) {
        log("Error: plugin_exit() failed to properly close BBV generation log file\n");
    }
    if (mav_collection) {
        if (fclose(mav_file)) {
            log("Error: plugin_exit() failed to properly close MAV file\n");
        }
    }

    profiling_pids.clear();
    total_prof_pids.clear();
    delete context;
    delete bbv;
}

void plugin_reset_callback(qemu_plugin_id_t id)
{
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, nullptr);
}

QEMU_PLUGIN_EXPORT
int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info, int argc, char **argv)
{
    /* Reset args and state in case plugin_init is called multiple within the
     * lifetime of a binary using this code (testing) */
    reset();

    if (!parse_args(argc, argv))
        return -1;

    /* Register to receive perf semihosting callbacks whenever HLT 0xf739 is
     * executed */
    if (qemu_plugin_semihosting_register(PERF_SEMIHOSTING_NUM, perf_semihosting_callback)) {
        log("Error registering perf semihosting callback\n");
        return -1;
    }

    if (mav_collection) {
        mav_file = fopen(mav_filename, "w+");
    }

    page_mask = (mav_blocksize - 1);
    pages = g_hash_table_new(nullptr, g_direct_equal);

    bbv = new BBV(bbv_filename, bbv_interval);
    context = new Context(id, &plugin_reset_callback, &update_profiling, 3 /* start in EL3 */, 0 /* initial PID=0 */);

    bbv_gen_logfile = fopen(bbv_gen_log, "w+");
    if (!bbv_gen_logfile) {
        log("plugin_init(): Error opening BBV generation log file for writing\n");
        return -1;
    }

    if (fputs("{\n\"version\": \"1\",\n\"checkpoints\": [", bbv_gen_logfile) < 0) {
        log("Error starting checkpoint file JSON array\n");
        return -1;
    }

    /* Tell the user what we're doing */
    log("Saving BBVs for ");
    if (profiling_pids.size()) {
        log("PID %" PRId64, *profiling_pids.begin());
    } else {
        log("first process with command name \"%s\"", profile_comm);
    }

    if (profile_children)
        log(" and its children,");

    log(" with interval-size %" PRId64, bbv_interval);
    log(" from EL0");
    if (profile_el1)
        log(" and EL1");
    log(" to %s (metadata to %s)\n", bbv_filename, bbv_gen_log);
    if (checkpoint_interval)
        log("Taking checkpoints every %" PRId64 " profiled instructions.\n", checkpoint_interval);

    // Call the callback here manually to originally register the hooks we want
    plugin_reset_callback(id);

    if (sizeof(uint64_t) != sizeof(void *)) {
        log("Error: `void *` is not the same size as `uint64_t`, which is relied upon by the BBV plugin!");
        exit(1);
    }

    return 0;
}
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
