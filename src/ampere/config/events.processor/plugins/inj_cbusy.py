# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import re

import evp

cbusy_csv = open(f"{output_dir}/inj_cbusy.csv", "w")
print("time,inj,high,low,max_outstanding_requests,outstanding_requests", file=cbusy_csv)


def inj_name(event_name):
    m = re.search("meshinjector_([0-9]+)", event_name)
    if m:
        return f"inj{m.group(1)}"
    else:
        return event_name


def cbusy_reset(event):
    """Log information from the CBusy reset event (occurs every CBusy window).

    This includes the number of high and low CBusy values seen, as well as the new
    maximum number of outstanding requests, and the actual number of outstanding
    requests."""
    inj = inj_name(event.name)
    high = event.data["high"]
    low = event.data["low"]
    max_outstanding_requests = event.data["max_outstanding_requests"]
    outstanding_requests = event.data["outstanding_requests"]
    print(f"{event.time},{inj},{high},{low},{max_outstanding_requests},{outstanding_requests}", file=cbusy_csv)


evp.on("*.meshinjector_#.cbusy.reset", cbusy_reset)
