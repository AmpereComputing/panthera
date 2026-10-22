//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_CORE_CP_NEXTLINE_H
#define AMPERE_CORE_CP_NEXTLINE_H

#include "ampere/core/prefetcher.h"
#include "ampere/sc/sc.h"
#include "ampere/sc/submodule.h"

namespace ampere::core {

using namespace ampere::sc;

class CP_NextLine : public Prefetcher<Fetch>, implementation<CP_NextLine, Prefetcher<Fetch>>
{
public:
    explicit CP_NextLine(sc_core::sc_module_name name);

protected:
    void train_impl(Fetch req, bool hit, bool prefetch, bool demand) override;

private:
    void table_print(std::ostream &os);

    // distance of 3 achieves highest performance
    Param<unsigned> distance{"distance", "how far to prefetch, in lines", 3};

    Event ev_train{"train", "NextLine was trained"};
    EventData<bool> ev_hitmiss{"train_by_hit", "NextLine trained by hit to a prefetched line"};
};

} // namespace ampere::core
#endif // AMPERE_CORE_CP_NEXTLINE_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
