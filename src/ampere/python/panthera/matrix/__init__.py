# Copyright (c) 2023-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import re
from collections import defaultdict

import numpy as np
import pandas as pd

from ..enums.chi_opcodes import *
from .render import Annotations

dir_to_name = ["south", "east", "north", "west", "up", "down", "port0", "port1", "port2", "port3"]


def rename_link(orig_name):
    """Match the rather ugly name coming from the event processor and turn it into a name
    like "x#y#.direction"
    """
    nsewud = re.match(r".*die_(\d+)\.mesh\.rtr_X(\d+)_Y(\d+)\.send_.*rtr_port:([012345])$", orig_name)
    devs = re.match(r".*die_(\d+)\.mesh\.rtr_X(\d+)_Y(\d+)\.((?:send)|(?:recv))_.*rtr_port:([6789])$", orig_name)

    if nsewud:
        die, x, y, rtr_port = nsewud.groups()
        try:
            return f"x{x}y{y}.{dir_to_name[int(rtr_port)]}"
        except IndexError:
            raise IndexError(f"router port {rtr_port} out of range in {orig_name}")
    elif devs:
        die, x, y, send_recv, rtr_port = devs.groups()
        return f"x{x}y{y}.{dir_to_name[int(rtr_port)]}.{'to_rtr' if send_recv == 'recv' else 'to_dev'}"
    else:
        return np.nan


def gen_utilization(in_df, ccl_clk_period=500, iod_clk_period=500):
    """Generate utilization metrics for all links.

    We calculate utilization by taking the count of the number of packets sent and
    dividing by the number of clock cycles in the sampled period. This requires us
    to know the clock period on both the CCL and IO Dies, which is unfortunately
    hard-coded for now.

    This function returns a new DataFrame shaped like this:

                          req   rsp0   rsp1   snp   dat   dat1  <-- channel
        die  link
        ------------------------------------------------------
         0   x0y0.south   45.0   2.0    6.2   0.0   20.1   0.0  <-- % of link utilized
         0   x0y0.east    ...

    """
    time_delta = (in_df["stop_time"] - in_df["start_time"]).values[0]
    ccl_cycles = time_delta / ccl_clk_period
    iod_cycles = time_delta / iod_clk_period

    out_dfs = []

    sum_cols = defaultdict(lambda: [])
    hover = {}

    for c in in_df.columns:
        m = re.match("(.*rtr_port:[0-9]+)/(chi_.*_opcode:.*)", c)
        if m:
            sum_cols[m.group(1)].append(m.group(0))

    new_sum_cols = {}

    for sum_col, cols in sum_cols.items():
        new_sum_cols[sum_col] = in_df[cols].sum(axis=1)

    in_df = pd.concat([in_df, pd.DataFrame(new_sum_cols)], axis=1)

    die_set = set()

    for sum_col, cols in sum_cols.items():
        link_name = rename_link(sum_col)
        die_m = re.match(".*die_([0-9]+)", sum_col)
        die = int(die_m.group(1))
        die_set.add(die)
        pcts = []
        total = new_sum_cols[sum_col].values[0]

        for col in cols:
            m = re.match(r".*(?:(?:send)|(?:recv))_([^/]+).*chi_(.*)_opcode:(\S+)", col)
            assert m
            chan_name, chan, opcode = m.groups()
            opcode_name = opcode
            pct = in_df[col].values[0] / total * 100.0

            if not pd.isna(pct):
                pcts.append(f"{opcode_name}: {pct:3.0f}%")

        hover[(die, link_name, chan_name)] = "  ".join(pcts)

    for die in die_set:
        # FIXME: the expression below is probably wrong for the new Domino
        cycles = ccl_cycles  # iod_cycles if die == 0 else ccl_cycles

        chan_dfs = []

        for chan in ("req", "req1", "rsp0", "rsp1", "snp", "dat", "dat1"):
            if chan == "rsp0":
                chan_name = "rsp"
            else:
                chan_name = chan
            tmp_df = pd.melt(
                in_df[
                    [
                        c
                        for c in in_df.columns
                        if re.match(
                            f".*die_{die}\\.mesh\\.rtr_X[0-9]+_Y[0-9]+\\.((send)|(recv))_{chan_name}/.*rtr_port", c
                        )
                    ]
                ]
            )
            tmp_df[chan] = tmp_df["value"] * 100.0 / cycles
            tmp_df["link"] = tmp_df["variable"].apply(rename_link)
            tmp_df[f"{chan}_title"] = tmp_df["link"].apply(lambda lnk: hover.get((die, lnk, chan_name), ""))
            tmp_df["die"] = die
            tmp_df = tmp_df.dropna().set_index(["die", "link"])
            chan_dfs.append(tmp_df[[chan, f"{chan}_title"]])

        all_chans = pd.concat(chan_dfs, axis=1)
        out_dfs.append(all_chans)

    return pd.concat(out_dfs)


def rename_ooc_link(orig_name):
    """Match the rather ugly name coming from the event processor and turn it into a name
    like "x#y#.direction".

    This is not quite the same as `rename_link` because we have a different format for the
    channel/port and we need to reverse the link from incoming to outgoing.
    """

    def reverse_link(x, y, direction):
        new_direction = {"n": "south", "s": "north", "e": "west", "w": "east"}[direction]

        x = int(x)
        y = int(y)

        if new_direction == "north":
            return x, y - 1, new_direction
        elif new_direction == "south":
            return x, y + 1, new_direction
        elif new_direction == "east":
            return x - 1, y, new_direction
        else:  # west
            return x + 1, y, new_direction

    nsew = re.match(
        r".*die_(\d+)\.mesh\.rtr_X(\d+)_Y(\d+)\.((?:req)|(?:rsp)|(?:snp)|(?:dat))([01]?)_([nsew])$", orig_name
    )
    devs = re.match(
        r".*die_(\d+)\.mesh\.rtr_X(\d+)_Y(\d+)\.((?:req)|(?:rsp)|(?:snp)|(?:dat))([01]?)_p([0-9]+)$", orig_name
    )
    dev_to_cpx = re.match(
        r".*cpx_die(\d+)_X(\d+)_Y(\d+)_P(\d+).dev_(\d+).((?:req)|(?:rsp)|(?:snp)|(?:dat))([01]?)_in", orig_name
    )
    rtr_to_cpxordev = re.match(
        r".*.die(\d+)_X(\d+)_Y(\d+)_P(\d+)_D(\d+)_((?:req)|(?:rsp)|(?:snp)|(?:dat))(?:_(?:rx)_)([01]?)_credit_pool",
        orig_name,
    )
    cpx_to_dev = re.match(
        r".*.die(\d+)_X(\d+)_Y(\d+)_P(\d+)_D(\d+)_((?:req)|(?:rsp)|(?:snp)|(?:dat))(?:_(?:tx)_)([01]?)_credit_pool",
        orig_name,
    )

    if nsew:
        die, x, y, chan, chan_num, direction = nsew.groups()
        # Unfortunately out-of-credit metrics are reported by the router that is the *destination* of the
        # link---i.e., the incoming connection---while the diagram is arranged to be annotated by the
        # outgoing connection. So we need to adjust x, y and direction to flip the link around.
        x, y, direction = reverse_link(x, y, direction)
        return pd.Series({"die": int(die), "link": f"x{x}y{y}.{direction}", "chan": f"{chan}{chan_num}"})
    elif devs:
        die, x, y, chan, chan_num, port = devs.groups()
        return pd.Series({"die": int(die), "link": f"x{x}y{y}.port{port}.to_rtr", "chan": f"{chan}{chan_num}"})
    elif dev_to_cpx:
        die, x, y, port, dev, chan, chan_num = dev_to_cpx.groups()
        return pd.Series({"die": int(die), "link": f"x{x}y{y}.port{port}.from_dev{dev}", "chan": f"{chan}{chan_num}"})
    elif cpx_to_dev:
        die, x, y, port, dev, chan, chan_num = cpx_to_dev.groups()
        return pd.Series({"die": int(die), "link": "x{x}y{y}.port{port}.to_dev{dev}", "chan": f"{chan}{chan_num}"})
    elif rtr_to_cpxordev:
        die, x, y, port, dev, chan, chan_num = rtr_to_cpxordev.groups()
        return pd.Series({"die": int(die), "link": f"x{x}y{y}.port{port}.to_dev{dev}", "chan": f"{chan}{chan_num}"})
    else:
        raise RuntimeError(f"link name {orig_name} did not match the expected patterns")


def gen_ooc(in_df):
    """Generate out-of-credit percentages for all links.

    This is very similar to utilization in many ways, but the renaming is different.
    The resulting DataFrame looks like this:

                          req   rsp0   rsp1   snp   dat   dat1  <-- channel
        die  link
        ------------------------------------------------------
         0   x0y0.south   45.0   2.0    6.2   0.0   20.1   0.0  <-- % of credit acquisitions that failed
         0   x0y0.east    ...
    """
    ooc_columns = [c for c in in_df.columns if "out_of_credits" in c and "rdi" not in c]
    acq_columns = [c.replace("out_of_credits", "acquire") for c in ooc_columns]

    def do_melt(wide_df, columns, value_name):
        tall_df = pd.melt(wide_df[columns], var_name="link", value_name=value_name)
        tall_df["link"] = tall_df["link"].apply(lambda x: x.replace(f".{value_name}", ""))
        return tall_df.set_index("link")

    ooc_df = do_melt(in_df, ooc_columns, "out_of_credits")
    acq_df = do_melt(in_df, acq_columns, "acquire")

    out_df = pd.concat([ooc_df, acq_df], axis=1).dropna()
    out_df = out_df[out_df["out_of_credits"] != 0]

    out_df["ooc_pct"] = (out_df["out_of_credits"] / (out_df["out_of_credits"] + out_df["acquire"])) * 100.0
    out_df.reset_index(inplace=True)
    out_df = out_df[out_df["link"].str.contains("memory") == False]

    out_df[["die", "link", "chan"]] = out_df["link"].apply(rename_ooc_link)
    out_df = out_df.pivot(index=["die", "link"], columns="chan", values="ooc_pct").fillna(0)
    if "req0" in out_df:
        out_df["req"] = out_df["req"] + out_df["req0"]
        out_df = out_df.drop(columns=["req0"])
    if "dat0" in out_df:
        if "dat" in out_df:
            out_df["dat"] = out_df["dat"] + out_df["dat0"]
            out_df = out_df.drop(columns=["dat0"])
        else:
            out_df = out_df.rename(columns={"dat0": "dat"})
    return out_df
