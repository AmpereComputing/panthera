//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/message_lite.h>

#pragma once

namespace ampere {
bool writeHeaderTo(uint32_t magic, uint32_t version, google::protobuf::io::ZeroCopyOutputStream* rawOutput);
bool readHeaderFrom(google::protobuf::io::ZeroCopyInputStream* rawInput, uint32_t* magic, uint32_t* version);
bool writeDelimitedTo(const google::protobuf::MessageLite& message,
                      google::protobuf::io::ZeroCopyOutputStream* rawOutput);
bool readDelimitedFrom(google::protobuf::io::ZeroCopyInputStream* rawInput,
                       google::protobuf::MessageLite* message,
                       bool* clean_eof);

inline bool clearAndReadDelimitedFrom(google::protobuf::io::ZeroCopyInputStream* rawInput,
                                      google::protobuf::MessageLite* message,
                                      bool* clean_eof)
{
    message->Clear();
    return readDelimitedFrom(rawInput, message, clean_eof);
}

} // namespace ampere

// Local Variables:
// mode: c++
// End:

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
