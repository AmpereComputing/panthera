//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_SC_RUNNABLE_H
#define AMPERE_SC_RUNNABLE_H

#include <limits>
#include "ampere/sc/cycle.h"

namespace ampere {
namespace sc {

const int UPDATE_PHASE = std::numeric_limits<int>::max();

class Runnable
{
public:
    virtual bool run() = 0;
    virtual void init() {}
    virtual bool is_channel() { return false; }

protected:
    void set_phase(int ph) { phase = ph; }

private:
    Cycle ran_in_cycle{-1};
    int phase{0};
    friend class Clock;
};

} // namespace sc
} // namespace ampere

#endif // AMPERE_SC_RUNNABLE_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
