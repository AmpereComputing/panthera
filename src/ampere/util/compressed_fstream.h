//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <cstdio>
#include <istream>
#include <memory>
#include <ostream>

namespace ampere::util {

enum CompressionType {
    NONE = 0,
    GZIP,
    ZLIB,
    BZ2,
    XZ,
};

CompressionType compression_from_filename(const char* filename);
std::unique_ptr<std::ostream> open_compressed_ostream(const char* filename,
                                                      std::ios_base::openmode mode = std::ios_base::out
                                                                                     | std::ios_base::trunc);
std::unique_ptr<std::istream> open_compressed_istream(const char* filename,
                                                      std::ios_base::openmode mode = std::ios_base::in);

FILE* open_xz_output_file(const char* filename);
FILE* open_xz_input_file(const char* filename);

} // namespace ampere::util

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
