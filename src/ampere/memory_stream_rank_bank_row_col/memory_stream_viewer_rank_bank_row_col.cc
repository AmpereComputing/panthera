//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <fcntl.h>
#include <iostream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "ampere/memory_stream_rank_bank_row_col/memory_stream_rank_bank_row_col.h"

using namespace ampere::msrbrc;

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::cout << "memory_stream_viewer [memory stream]" << std::endl;
        exit(1);
    }

    MemoryStreamReaderRankBankRowCol reader(argv[1]);
    Access_rbrc access;

    long access_count = 0, read_count = 0, write_count = 0;
    while (reader.read(access)) {
        access_count++;

        switch (access.type()) {
        case Access_rbrc_Type_READ:
            std::cout << "READ      : ";
            read_count++;
            break;
        case Access_rbrc_Type_WRITE:
            std::cout << "WRITE     : ";
            write_count++;
            break;
        default:
            std::cout << "UNKNOWN  : ";
            break;
        }

        std::cout << "mc : " << access.mc() << " ";
        std::cout << "rank : " << access.rank() << " ";
        std::cout << "bank : " << access.bank() << " ";
        std::cout << "row : " << access.row() << " ";
        std::cout << "col : " << access.col() << " ";
        std::cout << "pa : " << access.pa() << " ";
        std::cout << "offset : " << access.offset() << " " << std::endl;

        access.Clear();
    }
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
