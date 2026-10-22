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

#include "ampere/memory_stream/memory_stream.h"

using namespace ampere::ms;

static long data_count{0}, inst_count{0}, access_count{0}, bytes_count{0}, read_count{0}, bytes_read_count{0},
    write_count{0}, bytes_write_count{0}, readunique_count{0}, bytes_readunique_count{0};

std::set<uint64_t> unique_lines;

/* isolate access print so different versions can use */
void access_process(Access& access)
{
    access_count++;
    bytes_count += access.size();

    switch (access.source()) {
    case Access_Source_DATA:
        data_count++;
        std::cout << "DATA ";
        break;
    case Access_Source_INSTRUCTION:
        inst_count++;
        std::cout << "CODE ";
        break;
    default:
        std::cout << "OTHR ";
        break;
    }

    switch (access.type()) {
    case Access_Type_READ:
        std::cout << "READ      : ";
        read_count++;
        bytes_read_count += access.size();
        break;
    case Access_Type_WRITE:
        std::cout << "WRITE     : ";
        write_count++;
        bytes_write_count += access.size();
        break;
    case Access_Type_READUNIQUE:
        std::cout << "READUNIQUE: ";
        readunique_count++;
        bytes_readunique_count += access.size();
        break;
    default:
        std::cout << "UNKNOWN  : ";
        break;
    }

    std::cout << "pa: 0x" << std::hex << access.pa() << " ";
    std::cout << "va: 0x" << std::hex << access.va() << " ";
    std::cout << "size: " << std::dec << access.size() << " ";
    std::cout << "offset: " << std::dec << access.offset() << " ";
    std::cout << "agent_id: " << std::dec << access.agent_id() << " ";
    std::cout << "pc: 0x" << std::hex << access.pc() << " " << std::endl;

    unique_lines.insert(access.pa());

    access.Clear();
}

/* main parsing algorithm */
int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cout << "memory_stream_viewer [memory stream]" << std::endl;
        exit(EXIT_FAILURE);
    }

    MemoryStreamReader reader(argv[1]);
    Access access;
    Event event;

    bool parsing{true};

    std::cout << "Memory stream is version " << reader.version() << "\n" << std::endl;

    while (parsing) {
        switch (reader.version()) {
        case 1:
            // if version 1, read access
            if (reader.read(access)) {
                access_process(access);
            } else {
                parsing = false;
            }
            break;
        default:
            // if version 2 or higher, read event
            if (reader.read(event)) {
                if (event.has_access()) {
                    access = event.access();
                    access_process(access);

                } else if (event.has_control()) {
                    Control control = event.control();
                    std::cout << "CONTROL: ";

                    switch (control.type()) {
                    case Control_Type_START_MEMORY:
                        std::cout << "START_MEMORY";
                        break;
                    case Control_Type_END_MEMORY:
                        std::cout << "END_MEMORY";
                        break;
                    case Control_Type_START_CACHE_FLUSH:
                        std::cout << "START_CACHE_FLUSH";
                        break;
                    case Control_Type_END_CACHE_FLUSH:
                        std::cout << "END_CACHE_FLUSH";
                        break;
                    default:
                        std::cout << "UNKNOWN";
                        break;
                    }
                    std::cout << std::endl;
                }

                event.Clear();

            } else {
                parsing = false;
            }
            break;
        }
    }

    std::cout << std::dec << std::endl;
    if (reader.filter_was_unified()) {
        std::cout << "Note: Accesses filtered through unified " << reader.filter_dcache_size() << "-byte cache"
                  << std::endl
                  << std::endl;
    } else {
        std::cout << "Note: Accesses filtered through split " << reader.filter_dcache_size() << "-byte dcache and "
                  << reader.filter_icache_size() << "-byte icache." << std::endl
                  << std::endl;
    }
    std::cout << "total access count = " << access_count << std::endl;
    std::cout << "data count = " << data_count << std::endl;
    std::cout << "code count = " << inst_count << std::endl;
    std::cout << "read count = " << read_count << std::endl;
    std::cout << "write count = " << write_count << std::endl;
    std::cout << "readunique count = " << readunique_count << std::endl;
    std::cout << "total bytes count = " << bytes_count << std::endl;
    std::cout << "bytes read count = " << bytes_read_count << std::endl;
    std::cout << "bytes write count = " << bytes_write_count << std::endl;
    std::cout << "bytes readunique count = " << bytes_readunique_count << std::endl;
    std::cout << "unique lines = " << unique_lines.size() << std::endl;

    exit(EXIT_SUCCESS);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
