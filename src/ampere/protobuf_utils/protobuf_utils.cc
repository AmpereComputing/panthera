//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "protobuf_utils.h"

namespace ampere {

bool writeHeaderTo(uint32_t magic, uint32_t version, google::protobuf::io::ZeroCopyOutputStream* rawOutput)
{
    // We create a new coded stream for each message.  Don't worry, this is fast.
    google::protobuf::io::CodedOutputStream output(rawOutput);

    output.WriteLittleEndian32(magic);
    output.WriteLittleEndian32(version);

    return !output.HadError();
}

bool readHeaderFrom(google::protobuf::io::ZeroCopyInputStream* rawInput, uint32_t* magic, uint32_t* version)
{
    // We create a new coded stream for each message.  Don't worry, this is fast,
    // and it makes sure the 64MB total size limit is imposed per-message rather
    // than on the whole stream.  (See the CodedInputStream interface for more
    // info on this limit.)
    google::protobuf::io::CodedInputStream input(rawInput);

    return input.ReadLittleEndian32(magic) && input.ReadLittleEndian32(version);
}

bool writeDelimitedTo(const google::protobuf::MessageLite& message,
                      google::protobuf::io::ZeroCopyOutputStream* rawOutput)
{
    // We create a new coded stream for each message.  Don't worry, this is fast.
    google::protobuf::io::CodedOutputStream output(rawOutput);

    // Write the size.
    const int size = message.ByteSizeLong();
    output.WriteLittleEndian32(size);

    uint8_t* buffer = output.GetDirectBufferForNBytesAndAdvance(size);
    if (buffer != nullptr) {
        // Optimization:  The message fits in one buffer, so use the faster
        // direct-to-array serialization path.
        message.SerializeWithCachedSizesToArray(buffer);
    } else {
        // Slightly-slower path when the message is multiple buffers.
        message.SerializeWithCachedSizes(&output);
        if (output.HadError())
            return false;
    }

    return true;
}

bool readDelimitedFrom(google::protobuf::io::ZeroCopyInputStream* rawInput,
                       google::protobuf::MessageLite* message,
                       bool* clean_eof)
{
    // We create a new coded stream for each message.  Don't worry, this is fast,
    // and it makes sure the 64MB total size limit is imposed per-message rather
    // than on the whole stream.  (See the CodedInputStream interface for more
    // info on this limit.)
    google::protobuf::io::CodedInputStream input(rawInput);
    const int start = input.CurrentPosition();
    if (clean_eof)
        *clean_eof = false;

    // Read the size.
    uint32_t size;
    if (!input.ReadLittleEndian32(&size)) {
        if (clean_eof)
            *clean_eof = input.CurrentPosition() == start;
        return false;
    }
    // Tell the stream not to read beyond that size.
    google::protobuf::io::CodedInputStream::Limit limit = input.PushLimit(size);

    // Parse the message.
    if (!message->MergeFromCodedStream(&input))
        return false;
    if (!input.ConsumedEntireMessage())
        return false;

    // Release the limit.
    input.PopLimit(limit);

    return true;
}
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
