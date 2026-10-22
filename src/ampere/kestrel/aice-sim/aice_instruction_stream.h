//
// Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once
#include <boost/filesystem.hpp>
#include <memory>
#include "ampere/protobuf_utils/protobuf_stream.h"
#include "ampere/kestrel/aice-sim/aice_stream.pb.h"

namespace ampere::kestrel::aice::is {

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

using trace_writer_t = std::unique_ptr<InstructionStreamWriter>;

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

} // namespace ampere::kestrel::aice::is

