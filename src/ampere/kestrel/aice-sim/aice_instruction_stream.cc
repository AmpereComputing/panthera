//
// Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <iostream>
#include <cstdint>

#include "aice_instruction_stream.h"

namespace ampere::kestrel::aice::is {

const std::uint32_t protobuf_magic = 0x45434941; // "AICE" (little-endian)
const std::uint32_t protobuf_is_version = 1;     // synchronize with instruction_stream.py

InstructionStreamWriter::InstructionStreamWriter(boost::filesystem::path filepath, const Features features)
    : ProtobufStreamWriter(filepath, protobuf_magic, protobuf_is_version),
      started_measurement(false),
      stopped_measurement(false),
      finalize_called(false)
{
    if (!ProtobufStreamWriter::write(features)) {
        throw std::runtime_error("Unable to write features field to beginning of instruction stream");
    }
}

bool InstructionStreamWriter::write(const Instruction& instruction)
{
    Event event;
    event.mutable_instruction()->CopyFrom(instruction);
    return write(event);
}

bool InstructionStreamWriter::write(const Event& event)
{
    if (event.has_control()) {
        if (event.control().type() == Control_Type_START_MEASUREMENT) {
            if (started_measurement) {
                throw std::runtime_error("Attempted to insert start measurement marker into instruction stream twice");
            }
            started_measurement = true;
        } else if (event.control().type() == Control_Type_STOP_MEASUREMENT) {
            if (!started_measurement) {
                throw std::runtime_error(
                    "Attempted to insert stop measurement marker without previous start measurement marker");
            } else if (stopped_measurement) {
                throw std::runtime_error("Attempted to insert stop measurement marker into instruction stream twice");
            }
            stopped_measurement = true;
        }
    }
    return ProtobufStreamWriter::write(event);
}

void InstructionStreamWriter::finalize()
{
    finalize_called = true;
    if (!stopped_measurement) {
        throw std::runtime_error("Failed to insert stop measurement marker into instruction stream");
    }
}

InstructionStreamWriter::~InstructionStreamWriter()
{
    // If we are unwinding a stack due to an exception,
    // std::uncaught_exceptions will return non-zero. In
    // this case we assume the lack of finalization is
    // irrelevant. However if we are not unwinding due
    // to an exception, we want to print out a warning.

    if (!std::uncaught_exceptions() && !finalize_called) {
        std::cerr << "ERROR: InstructionStreamWriter not finalized" << std::endl;
    }
}

InstructionStreamReader::InstructionStreamReader(boost::filesystem::path filepath)
    : ProtobufStreamReader(filepath, protobuf_magic, protobuf_is_version)
{
    if (!ProtobufStreamReader::read(stream_features)) {
        throw std::runtime_error("Unable to read features field from beginning of stream");
    }
}

bool InstructionStreamReader::read(Instruction& instruction)
{
    Event event;
    bool status = true;
    while (status && !event.has_instruction())
        status = read(event);
    if (status) {
        instruction.Swap(event.mutable_instruction());
    }
    return status;
}

} // namespace ampere::kestrel::aice::is
