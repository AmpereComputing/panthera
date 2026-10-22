# Copyright (c) 2022-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import unittest

import example_pb2
from panthera.streams.protobuf_stream import *
from panthera.streams.protobuf_utils import *


class TestProtobufUtils(unittest.TestCase):
    def tearDown(self):
        if os.path.exists("example_test.es"):
            os.remove("example_test.es")

    def test_write_read_delimited(self):
        with open("example_test.es", "wb") as output:
            write_header_to(0x1234, 56, output)

            hello = example_pb2.Hello()
            hello.id = 1
            hello.message = "hello"
            write_delimited_to(hello, output)

            hello.id = 2
            hello.message = "world"
            write_delimited_to(hello, output)

        with open("example_test.es", "rb") as _input:
            magic, version = read_header_from(_input)
            self.assertEqual(magic, 0x1234)
            self.assertEqual(version, 56)

            hello = example_pb2.Hello()
            read_delimited_from(_input, hello)
            self.assertEqual(hello.id, 1)
            self.assertEqual(hello.message, "hello")

            read_delimited_from(_input, hello)
            self.assertEqual(hello.id, 2)
            self.assertEqual(hello.message, "world")


class TestProtobufStreams(unittest.TestCase):
    def tearDown(self):
        for filename in ["example_test.es", "example_test.es.xz"]:
            if os.path.exists(filename):
                os.remove(filename)

    def do_protobuf_reader_writer(self, filename):
        magic = 0x12345678
        version = 4
        writer = ProtobufStreamWriter(filename, magic, version)

        hello = example_pb2.Hello()
        hello.id = 42
        hello.message = "Hello, World!"

        writer.write(hello)
        del hello
        del writer

        reader = ProtobufStreamReader(filename, magic, 4)
        self.assertEqual(reader.version, 4)

        hello2 = example_pb2.Hello()
        reader.read(hello2)
        self.assertEqual(hello2.id, 42)
        self.assertEqual(hello2.message, "Hello, World!")

    def test_uncompressed(self):
        self.do_protobuf_reader_writer("example_test.es")

    def test_compressed(self):
        self.do_protobuf_reader_writer("example_test.es.xz")

    def do_long_protobuf_reader_writer(self, filename, num_records=256 * 1024, initial_buffer_size=None):
        magic = 0x12345678
        version = 4
        writer = ProtobufStreamWriter(filename, magic, version)

        hello = example_pb2.Hello()
        for i in range(num_records):
            hello.id = i
            hello.message = f"Hello, number {i}!"
            writer.write(hello)

        del hello
        del writer

        reader = ProtobufStreamReader(filename, magic, 4)
        if initial_buffer_size:
            reader.read_buffer_size = initial_buffer_size

        self.assertEqual(reader.version, 4)

        hello2 = example_pb2.Hello()
        for i in range(num_records):
            reader.read(hello2)

            self.assertEqual(hello2.id, i)
            self.assertEqual(hello2.message, f"Hello, number {i}!")

    def test_long_uncompressed(self):
        self.do_long_protobuf_reader_writer("example_test.es")

    def test_long_compressed(self):
        self.do_long_protobuf_reader_writer("example_test.es.xz")

    def test_long_compressed_small_initial_buffer(self):
        self.do_long_protobuf_reader_writer("example_test.es.xz", initial_buffer_size=1)
