//
// Copyright (c) 2019, 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <iostream>

#include "instruction_stream.h"

namespace ampere {
namespace is {

const uint32_t protobuf_magic = 0x53494250; // 0x50(P) 0x42(B) 0x49(I) 0x53(S)
const uint32_t protobuf_is_version = 6;     // synchronize with instruction_stream.py

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
    if (version() >= 4) {
        if (!ProtobufStreamReader::read(stream_features)) {
            throw std::runtime_error("Unable to read features field from beginning of stream");
        }
    } else {
        /*
         * Instruction stream versions lower than 4 lack feature bits in the
         * protobuf streams themselves, so we manually set them to the minimal
         * guaranteed support for these features.
         */

        // some version 3 streams in our study list have register values, but
        // some do not because we didn't increment the version when we added
        // them. Report 'false' for all these options since we cannot guarantee
        // support.
        stream_features.set_divide_sqrt_registers(false);
        stream_features.set_cache_maintenance_registers(false);
        stream_features.set_prefetch_registers(false);
        stream_features.set_all_destination_registers(false);

        // Though some translation information was present in earlier versions,
        // Memory writes were not guaranteed to be in program order prior to
        // version 3, so report false for earlier versions.
        stream_features.set_memory_translation(version() >= 3);

        // never supported earlier than version 4
        stream_features.set_memory_values(false);

        // new features for version 5:
        stream_features.set_sve_predicated_memops(false);
        stream_features.set_sve_predicate_registers(false);
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

} // namespace is
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
