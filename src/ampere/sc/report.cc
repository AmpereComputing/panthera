//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/sc/report.h"
#include "ampere/sc/ancestor.h"
#include "ampere/sc/environment.h"
#include "ampere/sc/module.h"

#include "sysc/kernel/sc_simcontext.h"
#include "sysc/utils/sc_report.h"

namespace ampere::sc {

void report_handler(const sc_core::sc_report& rep, const sc_core::sc_actions& actions)
{
    if (actions & sc_core::SC_DISPLAY) {
        std::cout << sc_core::sc_report_compose_message(rep) << std::endl;

        // preemptively call end_simulation for dump info before abort
        if ((actions & sc_core::SC_STOP) && (actions & sc_core::SC_ABORT)) {
            if (auto* context = sc_core::sc_get_curr_simcontext(); context)
                context->end();
        }
    }

    if (actions & sc_core::SC_ABORT) {
        // cleanly destruct event-stream (if found) before aborting
        if (auto* m = get_nearest<Module>(); m && m->get_environment())
            m->get_environment()->event_stream.reset();
        else if (auto* env = Environment::get_root(); env)
            env->event_stream.reset();
    }

    sc_core::sc_actions local_actions = actions & ~sc_core::SC_DISPLAY;
    sc_core::sc_report_handler::default_handler(rep, local_actions);
}

} // namespace ampere::sc
