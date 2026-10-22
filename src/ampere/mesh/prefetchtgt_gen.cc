//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/mesh/chi/opcodes.enum.h"
#include "ampere/mesh/prefetchtgt_gen.h"

namespace ampere::mesh {

void PrefetchTgtGen::accumulate(MeshDat dat)
{
    auto current_cycle = get_cycle();

    switch (dat_src_acc) {
    case DatSrcAcc::OFF:
        pft_off_cyc_count += current_cycle - last_acc_cycle;

        if (pft_off_cyc_count > pft_off_cyc_threshold) {
            dat_src_acc = DatSrcAcc::STARTUP;
            pft_off_cyc_count = 0;
        }
        break;
    case DatSrcAcc::STARTUP:
        if (++startup_wait_count > startup_wait_threshold)
            dat_src_acc = DatSrcAcc::ACTIVE;

        if (accum_in_startup) {
            ++rxdat_count;
            if (dat->DatSource == DataSource::PrefetchTgtUseful)
                ++rxdat_pft_successful_count;
        }
        break;
    case DatSrcAcc::ACTIVE:
        ++rxdat_count;

        if (dat->DatSource == DataSource::PrefetchTgtUseful)
            ++rxdat_pft_successful_count;

        if (rxdat_count >= rxdat_window) {
            rxdat_count = 0;
            rxdat_pft_successful_count = 0;

            if (rxdat_pft_successful_count < efficiency)
                dat_src_acc = DatSrcAcc::OFF;
        }
        break;
    }

    last_acc_cycle = current_cycle;
}

} // namespace ampere::mesh
