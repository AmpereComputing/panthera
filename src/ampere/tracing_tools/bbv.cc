//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <inttypes.h>
#include <stdlib.h>

#include "bbv.h"

VariableBBV::VariableBBV(const char *bbv_filename)
{
    outfile = fopen(bbv_filename, "w+");
    if (!outfile) {
        perror("init_vectors(): Error opening BBV file for writing\n");
        exit(1);
    }
}

void VariableBBV::record_bb(uint64_t pc, uint64_t num_insns)
{
    vector[pc] += num_insns;
}

void VariableBBV::end_bbv()
{
    char prefix = 'P';
    for (auto &bb : vector) {
        fprintf(outfile, "%c:0x%" PRIx64 ":%" PRId64, prefix, bb.first, bb.second);
        prefix = ' ';
    }
    fprintf(outfile, "\n");

    vector.clear();
}

VariableBBV::~VariableBBV()
{
    fclose(outfile);
}

BBV::BBV(const char *bbv_filename, uint64_t bbv_interval)
    : VariableBBV(bbv_filename), bbv_interval(bbv_interval), insn_count(0)
{
}

void BBV::record_bb(uint64_t pc, uint64_t num_insns)
{
    VariableBBV::record_bb(pc, num_insns);

    insn_count += num_insns;

    if (insn_count >= bbv_interval) {
        /* Subtracting bbv_interval instead of resetting to 0 has the effect of
         * causing us to average bbv_interval instructions per BBV, instead of
         * always exactly meeting or exceeding it */
        insn_count -= bbv_interval;

        end_bbv();
    }
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
