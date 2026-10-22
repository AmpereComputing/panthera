# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import re
from collections import defaultdict

import evp


def hnf_name(event_name):
    """Simplify an HNF event name to just 'hnf#'."""
    m = re.search("homenode_([0-9]+)", event_name)
    if m:
        return f"hnf{m.group(1)}"
    else:
        return event_name


cbusy_csv = open(f"{output_dir}/cbusy.csv", "w")
print("time,hnf,high,low,max_outstanding_requests,outstanding_requests", file=cbusy_csv)


tracker_csv = open(f"{output_dir}/tracker.csv", "w")
print("time,hnf,tracker_entries", file=tracker_csv)


def cbusy_reset(event):
    """Log information from the CBusy reset event (occurs every CBusy window).

    This includes the number of high and low CBusy values seen, as well as the new
    maximum number of outstanding requests, and the actual number of outstanding
    requests."""
    hnf = hnf_name(event.name)
    high = event.data["high"]
    low = event.data["low"]
    max_outstanding_requests = event.data["max_outstanding_requests"]
    outstanding_requests = event.data["outstanding_requests"]
    print(f"{event.time},{hnf},{high},{low},{max_outstanding_requests},{outstanding_requests}", file=cbusy_csv)


tracker_entries = defaultdict(lambda: 0)


def allocate_tracker(event):
    hnf = hnf_name(event.name)
    tracker_entries[hnf] += 1
    print(f"{event.time},{hnf},{tracker_entries[hnf]}", file=tracker_csv)


def deallocate_tracker(event):
    hnf = hnf_name(event.name)
    tracker_entries[hnf] -= 1
    print(f"{event.time},{hnf},{tracker_entries[hnf]}", file=tracker_csv)


evp.on("*.homenode_#.rcbusy.reset", cbusy_reset)
evp.on("*.homenode_#.allocate", allocate_tracker)
evp.on("*.homenode_#.deallocate", deallocate_tracker)
