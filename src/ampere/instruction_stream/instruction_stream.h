//
// Copyright (c) 2019, 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#ifndef AMPERE_INSTRUCTION_STREAM_H
#define AMPERE_INSTRUCTION_STREAM_H

#include "ampere/protobuf_utils/protobuf_stream.h"

#include "ampere/instruction_stream/instruction_stream.pb.h"

namespace ampere {
namespace is {

class InstructionStreamWriter : public ProtobufStreamWriter
{
public:
    InstructionStreamWriter(boost::filesystem::path filepath, const Features features);
    bool write(const Instruction& instruction);
    bool write(const Event& event);
    void finalize();
    ~InstructionStreamWriter();

private:
    bool started_measurement, stopped_measurement, finalize_called;
};

class InstructionStreamReader : public ProtobufStreamReader
{
public:
    InstructionStreamReader(boost::filesystem::path filepath);
    const Features features() { return stream_features; }
    bool read(Instruction& instruction);
    bool read(Event& event) { return ProtobufStreamReader::read(event); }

private:
    Features stream_features;
};

} // namespace is
} // namespace ampere

#endif // AMPERE_INSTRUCTION_STREAM_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
