#! /usr/bin/env python3
# Copyright (c) 2020-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Takes two event streams and shows the running drift, for performance
correlation purposes. Also rolls up per PC pushout and per mnemonic pushout.

Example usage and output:

../install/bin/panthera events.match --event sys.soc_0.core_0.gpc.instruction_commit \
    --es0 cassandra27_rtl.es --es0-label rtl \
    --es1 cassandra27_panthera.es --es1-label panthera
9997 instructions, 826 unique, IPC_rtl: 0.4691 IPC_panthera: 0.4494 Speedup: 0.958

"""

import logging
from itertools import islice

import matplotlib
import pandas as pd

from panthera import ArgumentParser, setup_logging
from panthera.streams.event_stream import EventStreamReader

matplotlib.use("Agg")


def parse_args():
    parser = ArgumentParser(description=__doc__, tool="events.tracing")
    parser.add_argument("--es0", default="events.es", help="event stream file")
    parser.add_argument("--es0-label", default="ES0", help="The label to use for ES0 in the output")
    parser.add_argument("--es1", default="events.es", help="event stream file (baseline)")
    parser.add_argument("--es1-label", default="ES1", help="The label to use for ES1 in the output")
    parser.add_argument("--n", type=int, default=30000, help="n instruction rollup")
    parser.add_argument("--event", action="append", help="event to include in output")
    parser.add_argument("--start", type=int, default=0, help="instruction number start")
    parser.add_argument("--end", type=int, default=30000, help="instruction number end")
    parser.add_argument("--o", required=True, help="output folder path and filename prefix")
    parser.add_argument(
        "--cycle_time",
        "-c",
        type=int,
        default=400,
        help="picoseconds for each CPU cycle",
    )
    parser.add_argument("--pc_metric", default="sys.soc_0.core_0.gpc.commit_pc", help="pc metric name")
    parser.add_argument("--inum_metric", default="sys.soc_0.core_0.gpc.inum", help="inum metric name")

    return parser.parse_args()


def load_es(path, events, pc_metric=None, inum_metric=None, cycle_time=400, start=0, end=30000):
    es = [
        (int(i.time / cycle_time), i.data[pc_metric], i.data[inum_metric])
        for i in islice(EventStreamReader(path, all_events=True).read_events(events), start, end)
    ]
    return pd.DataFrame.from_records(es, columns=["cycle", "pc", "inum"])


def align_es(*es):
    final_index = min(len(e) for e in es) - 1
    return final_index, [e.truncate(after=final_index) for e in es]


def ipc(es):
    return (es["inum"].max() - es["inum"].min()) / (es["cycle"].max() - es["cycle"].min())


def drift_table(es0, label0, es1, label1, start):
    final_index, (es0, es1) = align_es(es0, es1)
    inst_num = range(start, start + final_index + 1)
    pcs = es0["pc"].apply(hex)
    es0_delta = es0["cycle"].diff().fillna(0).astype(int)
    es1_delta = es1["cycle"].diff().fillna(0).astype(int)
    drift = es1_delta - es0_delta
    prog_drift = drift.cumsum()
    return pd.DataFrame(
        {
            "Inst": inst_num,
            "PC": pcs,
            f"{label0}_delta": es0_delta,
            f"{label1}_delta": es1_delta,
            "Drift": drift,
            "Progressive_drift": prog_drift,
        }
    )


def drift_plot(inst, label1, label0, output):
    line_plot = inst.plot(kind="line", x="Inst", y="Progressive_drift", c="Red")
    matplotlib.pyplot.title("Progressive Drift: Instruction Commit Cycle Difference")
    matplotlib.pyplot.xlabel("Instruction Number")
    matplotlib.pyplot.ylabel(f"Commit Cycle Difference ({label1} minus {label0})")
    matplotlib.pyplot.show(block=True)
    matplotlib.pyplot.savefig(output + "_progressive_drift.png", bbox_inches="tight")


def main(args):
    setup_logging()

    event_list = set(args.event) if args.event else set()
    start = max(1, args.start)
    end = min(start + args.n, args.end)

    (es0, es1) = [
        load_es(
            es,
            event_list,
            pc_metric=args.pc_metric,
            inum_metric=args.inum_metric,
            cycle_time=args.cycle_time,
            start=start,
            end=end,
        )
        for es in (args.es0, args.es1)
    ]

    ipc0 = ipc(es0)
    ipc1 = ipc(es1)

    inst = drift_table(es0, args.es0_label, es1, args.es1_label, start)
    inst.to_csv(args.o + ".csv")
    with open(args.o + ".pdtest.txt", "w") as f:
        f.write(inst.to_string(index=False))

    inst_by_pc = inst.groupby(by="PC").sum()
    inst_by_pc.sort_index().to_csv(args.o + "_group_by_pc_sort_by_pc.csv")
    inst_by_pc.sort_values("Drift").to_csv(args.o + "_group_by_pc_sort_by_drift.csv")

    # Summary table
    summary = f"{len(inst)} instructions, {inst['PC'].nunique()} unique, IPC_{args.es0_label}:{ipc0:.4f}, IPC_{args.es1_label}:{ipc1:.4f}, Speedup:{ipc0 / ipc1:.4f}"
    print(summary)
    with open(args.o + ".summary.txt", "w") as f:
        f.write(summary)

    # Create line graph of progressive drift
    drift_plot(inst, args.es1_label, args.es0_label, args.o)


if __name__ == "__main__":
    main(parse_args())
