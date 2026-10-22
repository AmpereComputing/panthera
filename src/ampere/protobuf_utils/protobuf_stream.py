#!/usr/bin/env python

# Copyright (c) 2022-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from io import DEFAULT_BUFFER_SIZE, IOBase

try:
    import lzma
except:
    from backports import lzma

from panthera.streams.protobuf_utils import *


class ProtobufStreamReader(object):
    def __init__(self, filename, expected_magic, max_version):
        if isinstance(filename, IOBase):
            self.file = filename
            self.close_when_done = False
        elif filename.endswith(".xz"):
            self.file = lzma.open(filename, "rb")
            self.close_when_done = True
        else:
            self.file = open(filename, "rb")
            self.close_when_done = True

        magic, version = read_header_from(self.file)
        self.version = version
        assert expected_magic == magic, (
            f"ProtobufStreamReader expected magic value to be {expected_magic}, but found {magic} instead"
        )
        assert self.version <= max_version, (
            f"ProtobufStreamReader expected version to be <={max_version}, but found {self.version} instead"
        )

        self.read_buffer_size = DEFAULT_BUFFER_SIZE
        self.read_buffer = bytearray(self.read_buffer_size)
        self.read_view = memoryview(self.read_buffer)
        self.read_index = 0
        self.write_index = 0

    def _read_file(self, bytes_to_read):
        """
        Handle reading from the file, but do the underlying read in
        buffer-sized chunks to increase the efficiency.
        """
        # Ensure that one buffer's worth is large enough to fit the item we are
        # reading. This avoids having to stitch together more than two buffers.
        if bytes_to_read > self.read_buffer_size:
            while bytes_to_read > self.read_buffer_size:
                self.read_buffer_size *= 2

            self.read_buffer = bytearray(self.read_buffer_size)
            self.read_buffer[self.read_index : self.write_index] = self.read_view[self.read_index : self.write_index]
            self.read_view.release()  # release the old bytearray from the view
            self.read_view = memoryview(self.read_buffer)

        if self.read_index + bytes_to_read >= self.write_index:
            # We're going to have to try to read another buffer's worth and
            # concatenate the results across buffers.
            remainder = bytearray(self.read_view[self.read_index : self.write_index])

            data = self.file.read(self.read_buffer_size)
            self.write_index = len(data)
            self.read_view[0 : self.write_index] = data
            self.read_index = min(bytes_to_read - len(remainder), self.write_index)

            return remainder + self.read_view[0 : self.read_index]
        else:
            # We can simply return a memoryview into the existing buffer.
            old_read_idx = self.read_index
            self.read_index += bytes_to_read
            return self.read_view[old_read_idx : self.read_index]

    def read(self, item):
        # Get message size in bytes, encoded as bytes
        num_bytes = self._read_file(4)
        if len(num_bytes) != 4:
            return False
        size = SIZE_STRUCT.unpack(num_bytes)[0]
        # Read the message from the file and decode it. Note that
        # ParseFromString handles clearing the protobuf 'message' object (named
        # 'item' here) so callers of this method don't need to.
        item.ParseFromString(bytes(self._read_file(size)))
        return True

    def close(self):
        if hasattr(self, "file") and self.close_when_done:
            self.file.close()

    def __del__(self):
        self.close()


class ProtobufStreamWriter(object):
    def __init__(self, filename, magic, version):
        if filename.endswith(".xz"):
            self.file = lzma.open(filename, "wb")
        else:
            self.file = open(filename, "wb")

        self.version = version
        write_header_to(magic, version, self.file)
        self.file.flush()

    def write(self, item):
        write_delimited_to(item, self.file)

    def close(self):
        if hasattr(self, "file"):
            self.file.flush()
            self.file.close()

    def __del__(self):
        self.close()
