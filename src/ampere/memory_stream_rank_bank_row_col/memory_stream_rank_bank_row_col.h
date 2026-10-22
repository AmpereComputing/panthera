//
// Copyright (c) 2020-2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_MEMORY_STREAM_RANK_BANK_ROW_COL_H
#define AMPERE_MEMORY_STREAM_RANK_BANK_ROW_COL_H

#include "ampere/protobuf_utils/protobuf_stream.h"

#include "ampere/memory_stream_rank_bank_row_col/memory_stream_rank_bank_row_col.pb.h"

namespace ampere {
namespace msrbrc {

class MemoryStreamWriterRankBankRowCol : public ProtobufStreamWriter
{
public:
    MemoryStreamWriterRankBankRowCol(boost::filesystem::path filepath, uint32_t cache_size /* bytes */);
    bool write(const Access_rbrc& access) { return ProtobufStreamWriter::write(access); }
};

class MemoryStreamReaderRankBankRowCol : public ProtobufStreamReader
{
public:
    MemoryStreamReaderRankBankRowCol(boost::filesystem::path filepath);
    bool read(Access_rbrc& access) { return ProtobufStreamReader::read(access); }
    uint32_t filter_cache_size() { return ms_cache_size; } // in bytes
private:
    uint32_t ms_cache_size; // The size, in bytes, of the cache this memory stream was filtered through
};

} // namespace msrbrc
} // namespace ampere

#endif // AMPERE_MEMORY_STREAM_RANK_BANK_ROW_COL_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
