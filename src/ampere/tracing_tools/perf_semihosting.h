//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef PERF_SEMIHOSTING_H
#define PERF_SEMIHOSTING_H

#include <strings.h>

#include "semihosting.h"

// perf_event_header.type values
#define PERF_RECORD_COMM 3
#define PERF_RECORD_EXIT 4
#define PERF_RECORD_FORK 7
#define PERF_RECORD_MMAP2 10
#define PERF_RECORD_SWITCH_CPU_WIDE 15
#define PERF_RECORD_NAMESPACES 16

// perf_event_header.misc bitfields:
// if PERF_RECORD_MMAP2 is not executable
#define PERF_RECORD_MISC_MMAP_DATA (1 << 13)
// if PERF_RECORD_COMM is on exec()
#define PERF_RECORD_MISC_COMM_EXEC (1 << 13)
// if PERF_RECORD_SWITCH_CPU_WIDE is switching *out*
#define PERF_RECORD_MISC_SWITCH_OUT (1 << 13)

struct perf_event_header
{
    uint32_t type;
    uint16_t misc;
    uint16_t size;
};

// PERF_RECORD_COMM
struct perf_comm_event
{
    struct perf_event_header header;
    uint32_t pid;
    uint32_t tid;
    char comm[];
};
// Maximum comm length in the kernel
#define TASK_COMM_LEN 16

inline bool perf_comm_matches(const perf_comm_event *evt, const char *str)
{
    return !strncasecmp(evt->comm, str, TASK_COMM_LEN - 1);
}

// PERF_RECORD_EXIT and PERF_RECORD_FORK
struct perf_task_event
{
    struct perf_event_header header;
    uint32_t pid, ppid;
    uint32_t tid, ptid;
    uint64_t time;
};

// PERF_RECORD_MMAP2
struct perf_mmap2_event
{
    struct perf_event_header header;
    uint32_t pid;
    uint32_t tid;
    uint64_t addr;
    uint64_t len;
    uint64_t pgoff;
    uint32_t maj;
    uint32_t min;
    uint64_t ino;
    uint64_t ino_generation;
    uint32_t prot;
    uint32_t flags;
    char filename[];
};

// PERF_RECORD_SWITCH_CPU_WIDE
struct perf_switch_event
{
    struct perf_event_header header;
    uint32_t next_prev_pid;
    uint32_t next_prev_tid;
};

// PERF_RECORD_NAMESPACES
struct perf_namespaces_event
{
    struct perf_event_header header;
    uint32_t pid;
    uint32_t tid;
    uint64_t nr_namespaces;
    struct
    {
        uint64_t dev, inode;
    } namespaces[/*nr_namespaces*/];
};

// Indices into perf_namespaces_event.namespaces
enum {
    NET_NS_INDEX = 0,
    UTS_NS_INDEX = 1,
    IPC_NS_INDEX = 2,
    PID_NS_INDEX = 3,
    USER_NS_INDEX = 4,
    MNT_NS_INDEX = 5,
    CGROUP_NS_INDEX = 6,
    NR_NAMESPACES, /* number of recognized namespaces (kernel may have additional) */
};

#endif /* PERF_SEMIHOSTING_H */

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
