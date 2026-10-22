//
// Copyright (c) 2020-2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_MEMORY_STREAM_H
#define AMPERE_MEMORY_STREAM_H

#include "ampere/protobuf_utils/protobuf_stream.h"

#include "ampere/memory_stream/memory_stream.pb.h"

namespace ampere {
namespace ms {

class MemoryStreamWriter : public ProtobufStreamWriter
{
public:
    MemoryStreamWriter(boost::filesystem::path filepath,
                       uint32_t dcache_size,
                       uint32_t icache_size,
                       bool unified = false);
    bool write(const Access& access);
    bool write(const Control& control);
    bool write(const Event& event) { return ProtobufStreamWriter::write(event); }
};

class MemoryStreamReader : public ProtobufStreamReader
{
public:
    MemoryStreamReader(boost::filesystem::path filepath);
    bool read(Access& access);
    bool read(Event& event) { return ProtobufStreamReader::read(event); }
    bool filter_was_unified() { return unified_filter; }
    uint32_t filter_dcache_size() { return ms_dcache_size; } // in bytes
    uint32_t filter_icache_size() { return ms_icache_size; } // in bytes
private:
    // unified_filter is true if the filter cache was unified (in which was the
    // values reported by filter_dcache_size() and filter_icache_size() will be
    // equal and both correspond to the *total* size of the cache.
    bool unified_filter;
    uint32_t ms_dcache_size; // The size, in bytes, of the dcache this memory stream was filtered through
    uint32_t ms_icache_size; // The size, in bytes, of the icache this memory stream was filtered through
};

} // namespace ms
} // namespace ampere

#endif // AMPERE_MEMORY_STREAM_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
