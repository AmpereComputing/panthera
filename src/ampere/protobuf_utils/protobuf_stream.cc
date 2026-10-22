//
// Copyright (c) 2021-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <iostream>
#include <sstream>
#include <cstdio>
#include <boost/make_unique.hpp>
#include <boost/format.hpp>
#include <boost/process/search_path.hpp>

#include "ampere/util/compressed_fstream.h"

#include "protobuf_stream.h"

namespace ampere {

ProtobufStreamWriter::ProtobufStreamWriter(int fd, uint32_t magic_number, uint32_t version)
    : output_stream{boost::make_unique<google::protobuf::io::FileOutputStream>(fd)}
{
    writeHeaderTo(magic_number, version, output_stream.get());
    flush();
}

ProtobufStreamWriter::ProtobufStreamWriter(boost::filesystem::path filepath,
                                           uint32_t magic_number,
                                           uint32_t version,
                                           bool force)
    : filepath(filepath), fp(nullptr)
{
    namespace bf = boost::filesystem;

    if (filepath.extension() == ".xz") {
        if (bf::exists(filepath)) {
            if (!force) {
                std::cerr << "ERROR: File already exists: " << filepath.string() << "\n";
                throw std::system_error(errno, std::system_category(), filepath.string());
            }

            std::remove(filepath.c_str());
        }

        fp = util::open_xz_output_file(filepath.c_str());
    } else {
        if (bf::exists(filepath)) {
            if (!force) {
                std::cerr << "ERROR: File already exists: " << filepath.string() << "\n";
                throw std::system_error(errno, std::system_category(), filepath.string());
            }

            std::remove(filepath.c_str());
        }

        fp = fopen(filepath.c_str(), "wx");
        if (!fp) {
            throw std::system_error(errno, std::system_category(), filepath.string());
        }
    }
    output_stream = boost::make_unique<google::protobuf::io::FileOutputStream>(fileno(fp));

    writeHeaderTo(magic_number, version, output_stream.get());
    flush();
}

ProtobufStreamWriter::~ProtobufStreamWriter()
{
    flush();
    if (fp != nullptr) {
        if (pclose(fp) == -1) {
            perror("Error calling pclose on xz's fd");
        }
    }
    output_stream.get()->Close();
}

bool ProtobufStreamWriter::flush()
{
    return output_stream.get()->Flush();
}

ProtobufStreamReader::ProtobufStreamReader(int fd, uint32_t magic_number, uint32_t max_version)
    : input_stream{boost::make_unique<google::protobuf::io::FileInputStream>(fd)}
{
    verify_file_version(magic_number, max_version);
}

ProtobufStreamReader::ProtobufStreamReader(boost::filesystem::path filepath,
                                           uint32_t magic_number,
                                           uint32_t max_version)
    : filepath(filepath), fp(nullptr)
{
    namespace bf = boost::filesystem;

    if (filepath.extension() == ".xz") {
        if (!bf::exists(filepath)) {
            throw std::system_error(errno, std::system_category(), filepath.string());
        }

        fp = util::open_xz_input_file(filepath.c_str());
    } else if (filepath.extension() == ".gz") {
        if (!bf::exists(filepath)) {
            throw std::system_error(errno, std::system_category(), filepath.string());
        }

        auto gunzippath = boost::process::search_path("gunzip");
        if (gunzippath.empty()) {
            throw std::system_error(errno, std::system_category(), "Cannot find `gunzip` on $PATH");
        }

        std::stringstream cmd;
        cmd << boost::format("%s -c %s") % gunzippath % filepath;
        fp = popen(cmd.str().c_str(), "r");
        if (fp == nullptr) {
            throw std::system_error(errno, std::system_category(), "popen() failed");
        }
    } else {
        fp = fopen(filepath.c_str(), "r");
        if (!fp) {
            throw std::system_error(errno, std::system_category(), filepath.string());
        }
    }
    input_stream = boost::make_unique<google::protobuf::io::FileInputStream>(fileno(fp));

    verify_file_version(magic_number, max_version);
}

void ProtobufStreamReader::verify_file_version(uint32_t magic_number, uint32_t max_version)
{
    uint32_t magic;
    readHeaderFrom(input_stream.get(), &magic, &stream_version);
    if (magic != magic_number) {
        throw std::runtime_error("Unrecognized magic number for protobuf stream");
    }
    if (stream_version > max_version) {
        throw std::runtime_error("Unrecognized protobuf stream version: " + std::to_string(stream_version));
    }
}

ProtobufStreamReader::~ProtobufStreamReader()
{
    int status(0);
    char buf[256];
    if (fp != nullptr) {
        status = pclose(fp);
        if (status == -1) {
            snprintf(buf, sizeof(buf), "Error calling pclose on xzcat or gunzip's fd (%s)", strerror(status));
            perror(buf);
        }
    } else if (fd != -1) {
        status = close(fd);
        if (status == -1) {
            perror("Error closing event stream file descriptor");
        }
    }
    input_stream.get()->Close();
}

} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
