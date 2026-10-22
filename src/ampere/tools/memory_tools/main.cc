//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
//
// Created by vagrant on 7/24/18.
//
#include <fcntl.h>
#include <iostream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "ampere/instruction_stream/instruction_stream.pb.h"
#include "ampere/protobuf_utils/protobuf_utils.h"
#include "ampere/archx/decoder_input.h"

#include <google/protobuf/io/zero_copy_stream_impl.h>

extern int decode_loads(const unsigned char* bytes);

int main(int argc, char** argv)
{
    int in_fd = 0;

    if (argc > 1) {
        in_fd = open(argv[1], O_RDONLY);

        if (in_fd < 0) {
            std::cerr << "error: " << strerror(errno) << std::endl;
            std::terminate();
        }
    }

    auto input_stream = new google::protobuf::io::FileInputStream(in_fd);
    ampere::is::Instruction instruction;

    uint64_t instruction_count = 0;
    uint64_t load_count = 0;

    while (ampere::readDelimitedFrom(input_stream, &instruction, nullptr)) {
        ampere::archx::DecoderInput di(instruction.opcode());
        if (decode_loads(di.ptr_no_mode())) {
            std::cout << "pc: " << std::hex << instruction.program_counter().va() << " "
                      << "op: " << instruction.opcode() << std::endl;
            ++load_count;
        }

        ++instruction_count;
        instruction.Clear();
    }

    if (in_fd != 0)
        close(in_fd);

    std::cout << std::endl << std::dec;
    std::cout << "instruction count = " << instruction_count << " load count = " << load_count << std::endl;

    return 0;
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
