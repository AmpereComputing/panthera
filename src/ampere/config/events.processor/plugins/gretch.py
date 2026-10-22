# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Summarize Gretch insights from an evp stream"""

import collections
from collections import Counter

import evp

cycle_time = 400
on_cycle = 16384
off_cycle = 65536
commit_times = []  # time of first commit
pref_cand = []  # #prefetch candidates, either original or replay
adt_trigger_time = [-1]  # time of latest ADT trigger
adt_epoch_time = [-1]  # time of latest ADT epoch
prod_fill_time = [-1]  # time of latest producer fill
launch_time = [-1]  # time of latest launch
all_adt_times = []  # times of all ADT triggers/epochs (for checking throttling)

adt_populate_cycles = []  # time for which ADT was being populated
prod_fill = {}  # producer fill count for each relationship
first_prod_fill_time = {}  # timestamp of first producer fill for each relationship
rel_insert = {}  # timestamps at which relationships are established
rel_evict = {}  # timestamps at which relationships are evicted
pref_dest = {}  # where do prefetch candidates end up
prod_launch_delay = []  # cycles between a producer fill and a launch
adt_trigger_pcs = {}  # frequency counts of each trigger PC
adt_trigger_pcs_times = {}

commits = []  # <time, pc>


def intercept_event(event):
    """Collect event info while consuming the event stream"""
    # #producer fills per relationship
    if event.name == "sys.soc_0.core_0.lsu.prefetcher.gretch.producer_fill":
        producer_pc = event.data["sys.soc_0.core_0.lsu.prefetcher.gretch.rt_producer_pc"]
        consumer_pc = event.data["sys.soc_0.core_0.lsu.prefetcher.gretch.rt_consumer_pc"]
        base_addr = event.data["sys.soc_0.core_0.lsu.prefetcher.gretch.rt_base_addr"]
        offset = event.data["sys.soc_0.core_0.lsu.prefetcher.gretch.rt_offset"]
        key = (producer_pc, consumer_pc, base_addr, offset)
        if key not in prod_fill:
            prod_fill[key] = 0
        prod_fill[key] += 1
        prod_fill_time[0] = event.time
        if key not in first_prod_fill_time:
            first_prod_fill_time[key] = event.time

    # time of first commit
    elif event.name == "sys.soc_0.core_0.gpc.instruction_commit":
        if len(commit_times) < 1:
            commit_times.append(event.time)
        commits.append((event.time, event.data["sys.soc_0.core_0.gpc.commit_pc"]))

    # timestamps at which relationships are established
    elif event.name == "sys.soc_0.core_0.lsu.prefetcher.gretch.rt_insert":
        producer_pc = event.data["sys.soc_0.core_0.lsu.prefetcher.gretch.rt_producer_pc"]
        consumer_pc = event.data["sys.soc_0.core_0.lsu.prefetcher.gretch.rt_consumer_pc"]
        base_addr = event.data["sys.soc_0.core_0.lsu.prefetcher.gretch.rt_base_addr"]
        offset = event.data["sys.soc_0.core_0.lsu.prefetcher.gretch.rt_offset"]
        key = (producer_pc, consumer_pc, base_addr, offset)
        if key not in rel_insert:
            rel_insert[key] = [event.time]
        else:
            rel_insert[key].append(event.time)

    # timestamps at which relationships are evicted
    elif event.name == "sys.soc_0.core_0.lsu.prefetcher.gretch.rt_evict":
        producer_pc = event.data["sys.soc_0.core_0.lsu.prefetcher.gretch.rt_producer_pc"]
        consumer_pc = event.data["sys.soc_0.core_0.lsu.prefetcher.gretch.rt_consumer_pc"]
        base_addr = event.data["sys.soc_0.core_0.lsu.prefetcher.gretch.rt_base_addr"]
        offset = event.data["sys.soc_0.core_0.lsu.prefetcher.gretch.rt_offset"]
        key = (producer_pc, consumer_pc, base_addr, offset)
        if key not in rel_evict:
            rel_evict[key] = [event.time]
        else:
            rel_evict[key].append(event.time)

    # where do pref candidates end up
    elif event.name in ["sys.soc_0.core_0.lsu.prefetcher.gretch.pq_insert", "sys.soc_0.core_0.lsu.pob.promote_replay"]:
        if len(pref_cand) < 1:
            pref_cand.append(0)
        pref_cand[0] += 1

    elif event.name == "sys.soc_0.core_0.lsu.prefetcher.gretch.launch":
        if "launch" not in pref_dest:
            pref_dest["launch"] = 0
        pref_dest["launch"] += 1
        launch_time[0] = event.time
        prod_launch_delay.append((launch_time[0] - prod_fill_time[0]) // cycle_time)

    elif event.name == "sys.soc_0.core_0.lsu.prefetcher.gretch.pq_dropped_full":
        if "pq_drop" not in pref_dest:
            pref_dest["pq_drop"] = 0
        pref_dest["pq_drop"] += 1

    elif event.name == "sys.soc_0.core_0.lsu.pob.dropped_full":
        if "pob_drop_full" not in pref_dest:
            pref_dest["pob_drop_full"] = 0
        pref_dest["pob_drop_full"] += 1

    elif event.name == "sys.soc_0.core_0.lsu.pob.dropped_mmu_noack":
        if "pob_drop_mmu_noack" not in pref_dest:
            pref_dest["pob_drop_mmu_noack"] = 0
        pref_dest["pob_drop_mmu_noack"] += 1

    elif event.name == "sys.soc_0.core_0.lsu.pob.dropped_fault":
        if "pob_drop_fault" not in pref_dest:
            pref_dest["pob_drop_fault"] = 0
        pref_dest["pob_drop_fault"] += 1

    elif event.name == "sys.soc_0.core_0.lsu.prefetcher.gretch.dropped_ofb_full":
        if "drop_ofb_full" not in pref_dest:
            pref_dest["drop_ofb_full"] = 0
        pref_dest["drop_ofb_full"] += 1

    elif event.name == "sys.soc_0.core_0.lsu.prefetcher.gretch.hit_dl1":
        if "hit_dl1" not in pref_dest:
            pref_dest["hit_dl1"] = 0
        pref_dest["hit_dl1"] += 1

    elif event.name == "sys.soc_0.core_0.lsu.prefetcher.gretch.hit_ofb":
        if "hit_ofb" not in pref_dest:
            pref_dest["hit_ofb"] = 0
        pref_dest["hit_ofb"] += 1

    # times between ADT trigger and ADT epoch
    elif event.name == "sys.soc_0.core_0.lsu.prefetcher.gretch.adt_trigger":
        adt_trigger_time[0] = event.time
        all_adt_times.append([event.time, -1])
        if "sys.soc_0.core_0.lsu.prefetcher.gretch.pc" in event.data:
            pcix = event.data["sys.soc_0.core_0.lsu.prefetcher.gretch.pc"]
        elif "program_counter" in event.data:
            pcix = event.data["program_counter"]
        if pcix not in adt_trigger_pcs:
            adt_trigger_pcs[pcix] = 0
        adt_trigger_pcs[pcix] += 1
        if pcix not in adt_trigger_pcs_times:
            adt_trigger_pcs_times[pcix] = [event.time]
        else:
            adt_trigger_pcs_times[pcix].append(event.time)
    elif event.name == "sys.soc_0.core_0.lsu.prefetcher.gretch.adt_epoch":
        adt_epoch_time[0] = event.time
        all_adt_times[-1][1] = event.time
        adt_populate_cycles.append((adt_epoch_time[0] - adt_trigger_time[0]) // cycle_time)
        adt_epoch_time[0] = -1
        adt_trigger_time[0] = -1

    ## Potential to-add in future ##
    # time between RT insertion and first producer fill
    # how many times do we have 2-hit RT entry and at what time
    # how to look at each batch of generated prefetch requests
    # check that replay gets priority


def during_on_cycle(cycle, on_regions):
    for i in range(len(on_regions)):
        if cycle >= on_regions[i][0] and cycle <= on_regions[i][1]:
            return i
    return -1


def el1_regions(commits_list):
    first_commit = commits_list[0][0]
    el1_regions = []
    el1_timestamps = []

    first_encountered = 0
    last_encountered = 0
    for k, v in commits_list:
        if str(hex(v)).startswith("0x1f") and first_encountered == 0:
            first_encountered = k
        elif str(hex(v)).startswith("0x1f") and first_encountered != 0:
            # we are in the EL1 region. compute the last
            last_encountered = k
        elif not str(hex(v)).startswith("0x1f") and first_encountered != 0:
            # we have reached the end of EL1
            el1_regions.append(
                ((first_encountered - first_commit) // cycle_time, (last_encountered - first_commit) // cycle_time)
            )
            el1_timestamps.append((first_encountered, last_encountered))
            first_encountered = 0
            last_encountered = 0

    return el1_regions, el1_timestamps


def on_regions(commits_list):
    last_commit_time = commits_list[-1][0]
    on_regions = []
    start = commits_list[0][0] // cycle_time
    stop = start + on_cycle
    on_regions.append((start, stop))

    for i in range(1, 5):
        start += on_cycle + off_cycle
        stop = start + on_cycle
        if start >= last_commit_time // cycle_time:
            break
        on_regions.append((start, stop))
        if stop >= last_commit_time // cycle_time:
            break

    return on_regions


def print_summary():
    print(
        "#### PRODUCER FILLS PER RELATIONSHIP, TIME OF FIRST PRODUCER FILL (absolute timestamp, cycle relative to first commit) ####"
    )
    for k, v in sorted(prod_fill.items(), key=lambda kv: kv[0][2]):
        assert k in first_prod_fill_time
        cycle_first_prod_fill = (first_prod_fill_time[k] - commit_times[0]) // cycle_time
        print([hex(i) for i in k], v, first_prod_fill_time[k], cycle_first_prod_fill)

    print("\n")

    print("#### INSERTION CYCLE OF USEFUL RELATIONSHIPS (absolute timestamp, cycle relative to first commit) ####")
    for k, v in sorted(rel_insert.items(), key=lambda kv: kv[0][2]):
        if k in prod_fill:
            print([hex(i) for i in k], v, [(v[i] - commit_times[0]) // cycle_time for i in range(len(v))])
    print("\n")

    print("#### EVICTION CYCLE OF USEFUL RELATIONSHIPS (absolute timestamp, cycle relative to first commit) ####")
    for k, v in sorted(rel_evict.items(), key=lambda kv: kv[0][2]):
        if k in prod_fill:
            print([hex(i) for i in k], v, [(v[i] - commit_times[0]) // cycle_time for i in range(len(v))])
    print("\n")

    print("#### INSERTION CYCLE OF ALL RELATIONSHIPS (absolute timestamp, cycle relative to first commit) ####")
    for k, v in sorted(rel_insert.items(), key=lambda kv: kv[0][2]):
        print([hex(i) for i in k], v, [(v[i] - commit_times[0]) // cycle_time for i in range(len(v))])
    print("\n")

    print("#### EVICTION CYCLE OF ALL RELATIONSHIPS (absolute timestamp, cycle relative to first commit) ####")
    for k, v in sorted(rel_evict.items(), key=lambda kv: kv[0][2]):
        print([hex(i) for i in k], v, [(v[i] - commit_times[0]) // cycle_time for i in range(len(v))])
    print("\n")

    print("#### OUTCOMES OF PREFETCH CANDIDATES (number, percentage) #####")
    for k, v in sorted(pref_dest.items(), key=lambda kv: kv[1], reverse=True):
        print("{: >20} {: >20} {: >20}%".format(*(k, v, round(v * 100 / pref_cand[0], 2))))
    print("\n")
    print("Prefetch candidates: " + str(pref_cand))
    print("\n")

    print("#### ADT INFORMATION #####")
    print("Num triggers = " + str(len(adt_populate_cycles)))
    print("Trigger PC frequency count: ")
    for k, v in sorted(adt_trigger_pcs.items(), key=lambda kv: kv[1]):
        print(hex(k), v)
    print("Trigger PC Times: ")
    for k, v in sorted(adt_trigger_pcs_times.items(), key=lambda kv: kv[1]):
        print(hex(k), [(v[i] - commit_times[0]) // cycle_time for i in range(len(v))])
        print(hex(k), v)
    print("\n")
    print("All ADT Trigger/Epoch Times (relative to first commit): ")
    print("On regions in cycles (relative to first commit): " + str(on_regions(commits)))
    el1_cycles, el1_timestamps = el1_regions(commits)
    print("EL1 regions in cycles (relative to first commit):" + str(el1_cycles))
    print("EL1 regions in timestamps:" + str(el1_timestamps))
    print("Time of first commit = " + str(commit_times[0]))
    for k, v in all_adt_times:
        k_rel = (k - commit_times[0]) // cycle_time
        v_rel = -1
        if v != -1:
            v_rel = (v - commit_times[0]) // cycle_time
        on_id = during_on_cycle(k_rel, on_regions(commits))  # whether commit lies in the on-period
        if on_id == -1:
            print(k_rel, v_rel, "OFF")
        else:
            print(k_rel, v_rel, on_id)

    print("\n")
    print("Population duration (cycles): " + str(sorted(adt_populate_cycles)))
    print("\n")

    print("#### CYCLES BETWEEN PRODUCER FILL AND LAUNCH (CYCLES, FREQUENCY) ####")
    freq = Counter(prod_launch_delay)
    for k, v in sorted(freq.items()):
        print(k, v)


evp.on("*", intercept_event)
evp.end_simulation(print_summary)
