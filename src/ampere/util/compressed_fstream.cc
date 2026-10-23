//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <cstring>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <cstdio>

#include <boost/iostreams/stream.hpp>
#include <boost/iostreams/device/file.hpp>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/bzip2.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/process/v1/search_path.hpp>
#include <memory>

#include "compressed_fstream.h"

namespace ampere::util {

struct open_process
{
    FILE* fp = nullptr;
    explicit open_process(FILE* fp) : fp(fp) {}
    ~open_process()
    {
        if (fp && pclose(fp) == -1)
            perror("Error closing");
    }
};

template<typename T>
class process_piped : public T
{
    // requires shared_ptr because boost::iostreams requires copy-constructor
    std::shared_ptr<open_process> fp;

public:
    explicit process_piped(FILE* fp)
        : T(fileno(fp), boost::iostreams::never_close_handle), fp(std::make_shared<open_process>(fp))
    {
    }
};

using process_sink = process_piped<boost::iostreams::file_descriptor_sink>;
using process_source = process_piped<boost::iostreams::file_descriptor_source>;

CompressionType compression_from_filename(const char* filename)
{
    std::filesystem::path filepath{filename};
    auto extension = filepath.extension();
    if (extension == ".gz") {
        return GZIP;
    } else if (extension == ".zlib" || extension == ".zl") {
        return ZLIB;
    } else if (extension == ".bz2") {
        return BZ2;
    } else if (extension == ".xz") {
        return XZ;
    }
    return NONE;
}

std::unique_ptr<std::ostream> open_compressed_ostream(const char* filename, std::ios_base::openmode mode)
{
    if (std::strlen(filename) == 1 && filename[0] == '-') {
        auto out = std::make_unique<boost::iostreams::filtering_ostream>();
        out->push(boost::ref(std::cout));
        return out;
    }

    auto compression = compression_from_filename(filename);
    if (compression == CompressionType::NONE)
        return make_unique<std::ofstream>(filename, mode);

    if ((mode & std::ios_base::app) || !(mode & std::ios_base::trunc))
        throw std::runtime_error("Compressed output streams require truncation; must not append.");

    if (compression == CompressionType::XZ) {
        // FIXME: if boost support is available, can use lzma_compressor
        auto* fp = open_xz_output_file(filename);
        return std::make_unique<boost::iostreams::stream<process_sink>>(fp);
    }

    auto out = std::make_unique<boost::iostreams::filtering_ostream>();

    if (compression == CompressionType::GZIP)
        out->push(boost::iostreams::gzip_compressor());
    else if (compression == CompressionType::ZLIB)
        out->push(boost::iostreams::zlib_compressor());
    else if (compression == CompressionType::BZ2)
        out->push(boost::iostreams::bzip2_compressor());
    out->push(boost::iostreams::file_sink(filename, mode | std::ios_base::binary));

    return out;
}

std::unique_ptr<std::istream> open_compressed_istream(const char* filename, std::ios_base::openmode mode)
{
    auto compression = compression_from_filename(filename);

    if (compression == CompressionType::NONE)
        return make_unique<std::ifstream>(filename, mode);

    if (compression == CompressionType::XZ) {
        // FIXME: if boost support is available, can use lzma_decompressor
        auto* fp = open_xz_input_file(filename);
        return std::make_unique<boost::iostreams::stream<process_source>>(fp);
    }

    auto in = std::make_unique<boost::iostreams::filtering_istream>();
    if (compression == CompressionType::GZIP)
        in->push(boost::iostreams::gzip_decompressor());
    else if (compression == CompressionType::ZLIB)
        in->push(boost::iostreams::zlib_decompressor());
    else if (compression == CompressionType::BZ2)
        in->push(boost::iostreams::bzip2_decompressor());

    in->push(boost::iostreams::file_source(filename, mode | std::ios_base::binary));

    return in;
}

FILE* open_xz_output_file(const char* filename)
{
    auto xzpath = boost::process::v1::search_path("xz");
    if (xzpath.empty())
        throw std::system_error(errno, std::system_category(), "Cannot find `xz` on $PATH");

    std::stringstream cmd;
    cmd << xzpath.string() << " -T 0 -z - > " << filename;
    auto* fp = popen(cmd.str().c_str(), "w");
    if (fp == nullptr)
        throw std::system_error(errno, std::system_category(), "popen() failed");

    return fp;
}

FILE* open_xz_input_file(const char* filename)
{
    auto xzcatpath = boost::process::v1::search_path("xzcat");
    if (xzcatpath.empty())
        throw std::system_error(errno, std::system_category(), "Cannot find `xzcat` on $PATH");

    std::stringstream cmd;
    cmd << xzcatpath.string() << " " << filename;
    auto* fp = popen(cmd.str().c_str(), "r");
    if (fp == nullptr)
        throw std::system_error(errno, std::system_category(), "popen() failed");

    return fp;
}

} // namespace ampere::util

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
