//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "ampere/memory_stream_rank_bank_row_col/memory_stream_rank_bank_row_col.h"

using namespace ampere::msrbrc;

int main()
{
    Access_rbrc access;
    // 256 READ requests to same bank/rank/row
    MemoryStreamWriterRankBankRowCol* writer = new MemoryStreamWriterRankBankRowCol("ms_same_bank_rank_row.ms", 1024);
    for (int i = 0; i < 256; i++) {
        access.set_type(Access_rbrc_Type_READ);
        access.set_rank(1);
        access.set_bank(1);
        access.set_row(0);
        access.set_col(0);
        access.set_offset(i);
        access.set_mc(0);
        access.set_pa(0xffffffffffffffff);
        writer->write(access);
    }
    delete writer;

    // 256 READ requests to different bank same rank/row
    MemoryStreamWriterRankBankRowCol* writer1 =
        new MemoryStreamWriterRankBankRowCol("ms_different_bank_same_rank_row.ms", 1024);
    for (int i = 0; i < 256; i++) {
        access.set_type(Access_rbrc_Type_READ);
        access.set_rank(0);
        access.set_bank(i);
        access.set_row(0);
        access.set_col(0);
        access.set_offset(i);
        access.set_mc(0);
        access.set_pa(0xffffffffffffffff);
        writer->write(access);
    }
    delete writer1;

    // 256 READ requests to same bank/rank/ different row
    MemoryStreamWriterRankBankRowCol* writer2 =
        new MemoryStreamWriterRankBankRowCol("ms_same_bank_rank_different_row.ms", 1024);
    for (int i = 0; i < 256; i++) {
        access.set_type(Access_rbrc_Type_READ);
        access.set_rank(1);
        access.set_bank(1);
        access.set_row(i);
        access.set_col(0);
        access.set_offset(i);
        access.set_mc(0);
        access.set_pa(0xffffffffffffffff);
        writer->write(access);
    }
    delete writer2;

    // 256 READ requests to same bank/rank/ different row
    MemoryStreamWriterRankBankRowCol* writer3 =
        new MemoryStreamWriterRankBankRowCol("ms_change_bank_rank_access.ms", 1024);
    int c = 0;
    for (int j = 0; j < 2; j++) {
        for (int i = 0; i < 64; i++) {
            access.set_type(Access_rbrc_Type_READ);
            access.set_rank(1);
            access.set_bank(1);
            access.set_row(i);
            access.set_col(0);
            access.set_offset(c);
            access.set_mc(0);
            access.set_pa(0xffffffffffffffff);
            writer3->write(access);
            c++;
        }
        for (int i = 64; i < 128; i++) {
            access.set_type(Access_rbrc_Type_READ);
            if (i < 32) {
                access.set_rank(1);
                access.set_bank(i);
            } else {
                access.set_rank(2);
                access.set_bank(i);
            }
            access.set_row(i);
            access.set_col(0);
            access.set_offset(c);
            access.set_mc(0);
            access.set_pa(0xffffffffffffffff);
            writer3->write(access);
            c++;
        }
    }
    delete writer3;

    // 64 requests, first/last requests are write/read requests to same bank/rank/row
    MemoryStreamWriterRankBankRowCol* writer4 = new MemoryStreamWriterRankBankRowCol("ms_read_after_write.ms", 1024);
    c = 0;
    for (int k = 0; k < 1; k++) {
        access.set_type(Access_rbrc_Type_WRITE);
        access.set_rank(0);
        access.set_bank(0);
        access.set_row(10);
        access.set_col(0);
        access.set_offset(c);
        access.set_mc(0);
        access.set_pa(0xffffffffffffffff);
        writer4->write(access);
        c++;

        for (int i = 2; i < 64; i++) {
            access.set_type(Access_rbrc_Type_READ);
            access.set_rank(2);
            access.set_bank(0);
            access.set_row(i);
            access.set_col(0);
            access.set_offset(c);
            access.set_mc(0);
            access.set_pa(0xffffffffffffffff);
            writer4->write(access);
            c++;
        }

        access.set_type(Access_rbrc_Type_READ);
        access.set_rank(0);
        access.set_bank(0);
        access.set_row(20);
        access.set_col(0);
        access.set_offset(c);
        access.set_mc(0);
        access.set_pa(0xffffffffffffffff);
        writer4->write(access);
        c++;
    }

    delete writer4;

    // 256 requests, to test closed/open page hits
    // every 64 requests we are going to switch between rows whicle accessing same rank/bank
    MemoryStreamWriterRankBankRowCol* writer5 = new MemoryStreamWriterRankBankRowCol("ms_cp_op_test.ms", 1024);
    c = 0;
    for (int k = 0; k < 128; k++) {
        access.set_type(Access_rbrc_Type_READ);
        access.set_rank(0);
        access.set_bank(0);
        access.set_row(0);
        access.set_col(0);
        access.set_offset(c);
        access.set_mc(0);
        access.set_pa(0xffffffffffffffff);
        writer5->write(access);
        c++;

        access.set_type(Access_rbrc_Type_READ);
        access.set_rank(0);
        access.set_bank(0);
        access.set_row(1);
        access.set_col(0);
        access.set_offset(c);
        access.set_mc(0);
        access.set_pa(0xffffffffffffffff);
        writer5->write(access);
        c++;
    }

    delete writer5;

    // 64 requests, no requests between write after read
    MemoryStreamWriterRankBankRowCol* writer6 =
        new MemoryStreamWriterRankBankRowCol("ms_read_after_write_zero_requests.ms", 1024);
    c = 0;
    for (int i = 0; i < 32; i++) {
        access.set_type(Access_rbrc_Type_WRITE);
        access.set_rank(0);
        access.set_bank(0);
        access.set_row(10);
        access.set_col(0);
        access.set_offset(c);
        access.set_mc(0);
        access.set_pa(0xffffffffffffffff);
        writer6->write(access);
        c++;

        access.set_type(Access_rbrc_Type_READ);
        access.set_rank(0);
        access.set_bank(0);
        access.set_row(20);
        access.set_col(0);
        access.set_offset(c);
        access.set_mc(0);
        access.set_pa(0xffffffffffffffff);
        writer6->write(access);
        c++;
    }

    delete writer6;
    return 0;
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
