//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_BBV_H
#define AMPERE_BBV_H

#include <stdio.h>
#include <unordered_map>

class VariableBBV
{
public:
    VariableBBV(const char* bbv_filename);
    void record_bb(uint64_t pc, uint64_t insn_count);
    void end_bbv();
    ~VariableBBV();

private:
    FILE* outfile;
    std::unordered_map<uint64_t, uint64_t> vector;
};

class BBV : private VariableBBV
{
public:
    BBV(const char* bbv_filename, uint64_t bbv_interval);
    void record_bb(uint64_t pc, uint64_t insn_count);

private:
    uint64_t bbv_interval;
    uint64_t insn_count;
};

#endif /* AMPERE_BBV_H */

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
