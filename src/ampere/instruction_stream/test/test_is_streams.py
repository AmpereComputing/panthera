# Copyright (c) 2022-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import unittest

from panthera.streams import instruction_stream_pb2 as is_proto
from panthera.streams.instruction_stream import *

test_dir = os.path.dirname(__file__)


class TestInstructionStreamReader(unittest.TestCase):
    def do_read_test(self, name):
        fullname = os.path.join(test_dir, name)
        reader = InstructionStreamReader(fullname)

        instruction = is_proto.Instruction()
        self.assertTrue(reader.read(instruction))
        self.assertTrue(reader.read(instruction))
        self.assertFalse(reader.read(instruction))
        self.assertEqual(reader.version, 1)

        self.assertFalse(reader.features.divide_sqrt_registers)
        self.assertFalse(reader.features.cache_maintenance_registers)
        self.assertFalse(reader.features.memory_translation)
        self.assertFalse(reader.features.memory_values)

    def test_read(self):
        self.do_read_test("test.is")

    def test_read_xz(self):
        self.do_read_test("test.is.xz")

    def test_nonexistent(self):
        with self.assertRaises(Exception):
            self.do_read_test("doesnotexist.is")

    def test_nonexistent_xz(self):
        with self.assertRaises(Exception):
            self.do_read_test("doesnotexist.is.xz")


class TestInstructionStreamWriter(unittest.TestCase):
    def tearDown(self):
        path = os.path.join(test_dir, "python_is_writer.is")
        if os.path.exists(path):
            os.remove(path)

    def write_control(self, writer, _type):
        event = is_proto.Event()
        control = is_proto.Control()
        control.type = _type
        event.control.CopyFrom(control)
        writer.write(event)

    def test_write(self):
        features = is_proto.Features()
        features.divide_sqrt_registers = False
        features.cache_maintenance_registers = True
        features.memory_translation = True
        features.memory_values = False

        writer = InstructionStreamWriter(os.path.join(test_dir, "python_is_writer.is"), features)

        instruction = is_proto.Instruction()
        instruction.opcode = 0xFFFFFFFF
        writer.write(instruction)

        self.write_control(writer, is_proto.Control.Type.Value("START_MEASUREMENT"))

        instruction.opcode = 0xEEEEEEEE
        memop = instruction.memop.add()
        memop.va = 0xAAAAAAAAAAAAAAAA
        writer.write(instruction)

        self.write_control(writer, is_proto.Control.Type.Value("STOP_MEASUREMENT"))
        del writer

        reader = InstructionStreamReader(os.path.join(test_dir, "python_is_writer.is"))
        self.assertFalse(reader.features.divide_sqrt_registers)
        self.assertTrue(reader.features.cache_maintenance_registers)
        self.assertTrue(reader.features.memory_translation)
        self.assertFalse(reader.features.memory_values)

        event = is_proto.Event()
        self.assertTrue(reader.read(event))
        self.assertTrue(event.HasField("instruction"))
        self.assertEqual(event.instruction.opcode, 0xFFFFFFFF)
        self.assertEqual(len(event.instruction.memop), 0)

        event.Clear()
        self.assertTrue(reader.read(event))
        self.assertTrue(event.HasField("control"))
        self.assertEqual(event.control.type, is_proto.Control.Type.Value("START_MEASUREMENT"))

        instruction = is_proto.Instruction()
        self.assertTrue(reader.read(instruction))
        self.assertEqual(instruction.opcode, 0xEEEEEEEE)
        self.assertEqual(len(instruction.memop), 1)
        self.assertEqual(instruction.memop[0].va, 0xAAAAAAAAAAAAAAAA)

        event.Clear()
        self.assertTrue(reader.read(event))
        self.assertTrue(event.HasField("control"))
        self.assertEqual(event.control.type, is_proto.Control.Type.Value("STOP_MEASUREMENT"))

        self.assertFalse(reader.read(event))
