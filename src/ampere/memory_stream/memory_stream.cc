//
// Copyright (c) 2020-2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "memory_stream.h"

namespace ampere {
namespace ms {

const uint32_t protobuf_magic = 0x534D4250; // 0x50(P) 0x42(B) 0x4D(M) 0x53(S)
const uint32_t protobuf_ms_version = 4;

MemoryStreamWriter::MemoryStreamWriter(boost::filesystem::path filepath,
                                       uint32_t dcache_size,
                                       uint32_t icache_size /* cache sizes in bytes */,
                                       bool unified)
    : ProtobufStreamWriter(filepath, protobuf_magic, protobuf_ms_version)
{
    Filter filter;

    if (unified && dcache_size != icache_size)
        throw std::runtime_error(
            "Data and Instruction filter caches must be set to the same size for a unified filter cache");

    filter.set_instruction_data_split(!unified);
    if (dcache_size)
        filter.set_cache_size(dcache_size);
    if (icache_size)
        filter.set_icache_size(icache_size);

    if (!ProtobufStreamWriter::write(filter)) {
        throw std::runtime_error("Unable to write cache filter event to beginning of memory stream");
    }
}

bool MemoryStreamWriter::write(const Access& access)
{
    Event event;
    event.mutable_access()->CopyFrom(access);
    return write(event);
}

bool MemoryStreamWriter::write(const Control& control)
{
    Event event;
    event.mutable_control()->CopyFrom(control);
    return write(event);
}

MemoryStreamReader::MemoryStreamReader(boost::filesystem::path filepath)
    : ProtobufStreamReader(filepath, protobuf_magic, protobuf_ms_version)
{
    Filter filter;
    if (ProtobufStreamReader::read(filter)) {
        unified_filter = !filter.instruction_data_split();
        ms_dcache_size = filter.cache_size();
        if (unified_filter) {
            if (version() >= 3 && ms_dcache_size != filter.icache_size())
                throw std::runtime_error(
                    "Failing to read a version >= 3 memory streams which reports it has a unified filter cache, but "
                    "has its dcache and icache filter sizes set differently");
            ms_icache_size = ms_dcache_size;
        } else {
            ms_icache_size = filter.icache_size();
        }
    } else {
        throw std::runtime_error("Unable to read cache filter event from beginning of memory stream");
    }
}

bool MemoryStreamReader::read(Access& access)
{
    if (1 == version()) {
        return ProtobufStreamReader::read(access);
    } else {
        Event event;
        bool status = true;
        while (status && !event.has_access())
            status = read(event);
        if (status) {
            access.Swap(event.mutable_access());
        }
        return status;
    }
}

} // namespace ms
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
