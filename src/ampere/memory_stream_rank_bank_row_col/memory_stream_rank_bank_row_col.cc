//
// Copyright (c) 2020-2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "memory_stream_rank_bank_row_col.h"

namespace ampere {
namespace msrbrc {

const uint32_t protobuf_magic = 0x53434D50; // 0x50(P) 0x4D(M) 0x43(C) 0x53(S)
const uint32_t protobuf_ms_version = 1;

MemoryStreamWriterRankBankRowCol::MemoryStreamWriterRankBankRowCol(boost::filesystem::path filepath,
                                                                   uint32_t cache_size /* bytes */)
    : ProtobufStreamWriter(filepath, protobuf_magic, protobuf_ms_version)
{
    Filter filter;
    filter.set_cache_size(cache_size);
    if (!ProtobufStreamWriter::write(filter)) {
        throw std::runtime_error("Unable to write cache filter event to beginning of memory stream");
    }
}

MemoryStreamReaderRankBankRowCol::MemoryStreamReaderRankBankRowCol(boost::filesystem::path filepath)
    : ProtobufStreamReader(filepath, protobuf_magic, protobuf_ms_version)
{
    Filter filter;
    if (ProtobufStreamReader::read(filter)) {
        ms_cache_size = filter.cache_size();
    } else {
        throw std::runtime_error("Unable to read cache filter event from beginning of memory stream");
    }
}

} // namespace msrbrc
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
