#
# Copyright (c) 2022 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#

import panthera.streams.instruction_stream_pb2 as is_proto
from panthera.streams.protobuf_stream import ProtobufStreamReader, ProtobufStreamWriter

protobuf_is_magic = 0x53494250  # 0x50(P) 0x42(B) 0x49(I) 0x53(S)
protobuf_is_version = 6  # synchronize with instruction_stream.cc


class InstructionStreamWriter(ProtobufStreamWriter):
    def __init__(self, filename, features):
        """Initialize an instruction stream for writing, including writing the
        passed-in features, which should be an instance of
        panthera.streams.instruction_stream_pb2.Features, to the stream"""
        super().__init__(filename, protobuf_is_magic, protobuf_is_version)
        self.write(features)

    def write_instruction(self, instruction):
        event = is_proto.Event()
        event.instruction.CopyFrom(instruction)
        super().write(event)

    def write(self, msg):
        if isinstance(msg, is_proto.Event):
            super().write(msg)
        elif isinstance(msg, is_proto.Instruction):
            self.write_instruction(msg)
        elif isinstance(msg, is_proto.Features):
            super().write(msg)
        else:
            raise Exception(f"InstructionStreamWriter.write called with unexpected message type: {type(msg).__name__}")


class InstructionStreamReader(ProtobufStreamReader):
    def __init__(self, filename):
        """Initialize an instruction stream for reading, including reading the
        on-disk features"""
        super().__init__(filename, protobuf_is_magic, protobuf_is_version)
        self.features = is_proto.Features()
        if self.version >= 4:
            self.read(self.features)
        else:
            # see comments in instruction_stream.cc for detailed explanation of
            # why these are set this way
            self.features.divide_sqrt_registers = False
            self.features.cache_maintenance_registers = False
            self.features.prefetch_registers = False
            self.features.all_destination_registers = False
            self.features.memory_translation = self.version >= 3
            self.features.memory_values = False
            self.features.sve_predicated_memops = False
            self.features.sve_predicate_registers = False

    def read_instruction(self, instruction):
        event = is_proto.Event()
        status = super().read(event)
        while status and not event.HasField("instruction"):
            status = super().read(event)
        if status:
            instruction.CopyFrom(event.instruction)
        return status

    def read(self, msg):
        if isinstance(msg, is_proto.Event):
            return super().read(msg)
        elif isinstance(msg, is_proto.Instruction):
            return self.read_instruction(msg)
        elif isinstance(msg, is_proto.Features):
            return super().read(msg)
        else:
            raise Exception(f"InstructionStreamReader.read called with unexpected message type: {type(msg).__name__}")
