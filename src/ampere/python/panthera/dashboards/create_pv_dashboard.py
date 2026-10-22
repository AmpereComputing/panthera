# Copyright (c) 2025-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import argparse

import holoviews as hv
import pandas as pd
import panel as pn
from bokeh.models.widgets.tables import BooleanFormatter
from panthera.matrix.data import Data
from panthera.rollup.soc import calc_bw, calc_lat

pn.extension(template="material", sizing_mode="stretch_width")
hv.extension("bokeh", "matplotlib")


def get_bb_data(full_df, workload):
    cols = [
        "start_time",
        "stop_time",
        "workload",
        "interval",
        "mem_size",
        "mem_send_dat",
        "mem_recv_dat",
        "inj0_latency",
        "sys.soc_0.core_0.send_req/ReadShared/allow_retry:1",
        "sys.soc_0.core_0.send_req/ReadShared/allow_retry:0",
    ]
    df = full_df[cols]
    df = df[df["workload"] == workload]
    df["core"] = 0
    df["latency"] = df["inj0_latency"]
    df["bandwidth"] = 64 * calc_bw(df, "mem_send_dat", "mem_recv_dat")
    df = df.sort_values("interval")
    return df


def draw_lat_bb_tab(bb_df):
    # Workload dropdown box
    ui_workload = pn.widgets.Select(name="Workload:", value="1r0w", options=["1r0w", "0r1w", "1r1w"])

    # BW/Lat Graph Component
    @pn.depends(ui_workload.param.value)
    def draw_bw_lat(workload):
        configs = bb_df["cfg"].unique()
        curves = []
        for config in configs:
            df = bb_df[bb_df["cfg"] == config]
            bb_data = get_bb_data(df, workload)
            # print(f"Config[{config}]:\n{bb_data}")
            curve = (
                hv.Curve(bb_data, "bandwidth", "latency", label=config)
                * hv.Scatter(bb_data, "bandwidth", "latency")
                * hv.Labels(bb_data, ["bandwidth", "latency"], "interval").opts(xoffset=2, yoffset=-3)
            )
            curves.append(curve)

        overlay = hv.Overlay(curves).opts(
            ylim=(0, None),
            height=500,
            responsive=True,
            tools=["hover"],
            xlabel="Bandwith (GB/s)",
            ylabel="Latency (ns)",
            title="Panthera vs. Simulation PV (BW/Lat)",
        )
        return overlay

    return pn.Column(ui_workload, draw_bw_lat)


def get_verify_table(df):
    request_event = "sys.soc_0.core_0.send_req/ReadShared/allow_retry"
    df["inj0_latency_txns"] = df[f"{request_event}:0"].fillna(0) + df[f"{request_event}:1"].fillna(0)
    df["stash_accepts"] = df.filter(regex="sys.soc_0.core_[0-9]+.send_rsp/SnpResp/data_pull:1").sum(axis=1)
    df["stash_rejects"] = df.filter(regex="sys.soc_0.core_[0-9]+.send_rsp/SnpResp/data_pull:0").sum(axis=1).round()
    df["stash_reject_rate"] = (df["stash_rejects"] / (df["stash_accepts"] + df["stash_rejects"]).fillna(0) * 100).round(
        1
    ).astype(str) + "%"

    # cols = ['workload', 'cfg', 'interval', 'mem_size', 'inj0_latency_txns', 'verify_pxha_eq_distributed', 'verify_pmha_eq_distributed', 'verify_retry_core_retry_rate', 'verify_stash_reject_rate', 'verify_matrix']
    cols = [
        "workload",
        "cfg",
        "interval",
        "mem_size",
        "inj0_latency_txns",
        "verify_pxha_eq_distributed",
        "verify_pmha_eq_distributed",
        "verify_retry_core_retry_rate",
        "verify_cleaninvalid_rate",
        "stash_reject_rate",
    ]
    df = df[cols]
    bokeh_formatters = {
        "verify_pxha_eq_distributed": BooleanFormatter(),
        "verify_pmha_eq_distributed": BooleanFormatter(),
        "verify_retry_core_retry_rate": BooleanFormatter(),
        "verify_cleaninvalid_rate": BooleanFormatter(),
        #'verify_stash_reject_rate': BooleanFormatter(),
        #'verify_mem_streaming': BooleanFormatter(),
    }
    column_names = {
        "workload": "Workload",
        "cfg": "Config",
        "interval": "Interval",
        "mem_size": "Size",
        "inj0_latency_txns": "Latency Reads",
        "verify_pxha_eq_distributed": "Eq Dist (PxHA)",
        "verify_pmha_eq_distributed": "Eq Dist (PmHA)",
        "verify_retry_core_retry_rate": "Retry Rate Low",
        "verify_cleaninvalid_rate": "Core CleanInvalid Rate Low",
        "verify_stash_reject_rate": "Stash Reject Rate",
        #'verify_mem_streaming': 'Verify Streaming Memory',
        "verify_matrix": "Verified",
    }

    return pn.widgets.Tabulator(
        df,
        layout="fit_columns",
        header_filters=True,
        titles=column_names,
        formatters=bokeh_formatters,
        show_index=False,
    )


def draw_verify_tab(bb):
    return get_verify_table(bb)


def draw_idle_lat_tab():
    lat_breakdown_df = data.get("req2_req_latencysummary", experiment="bb")
    pass


def main():
    parser = argparse.ArgumentParser(description="Generate PV Dashboard HTML file.")
    parser.add_argument("-o", "--output", type=str, help="Output html filename", required=True)
    parser.add_argument("-p", "--panthera-data", type=str, help="Data directory of panthera run", required=True)
    parser.add_argument("-r", "--rtl-data", type=str, help="Data directory of rtl run", required=True)
    args = parser.parse_args()

    # Get data in df
    panthera_data = Data(args.panthera_data, enable_cache=False)
    bb_panthera = panthera_data.get("summary", experiment="bb")
    rtl_data = Data(args.rtl_data, enable_cache=False)
    bb_rtl = rtl_data.get("summary", experiment="bb")
    bb = pd.concat([bb_panthera, bb_rtl])

    # Rename config column
    bb["cfg"] = bb["cfg"].map({"rtl-dn-octant": "RTL", "dn-octant-rtl": "Panthera"})

    top = pn.Tabs()
    top.append(("Verify Table", draw_verify_tab(bb)))
    top.append(("BW/Lat", draw_lat_bb_tab(bb)))
    # top.append(("Idle Latency", draw_idle_lat_tab()))

    top.servable(title="Matrix Performance Validation")
    top.save(args.output, title="Matrix PV", resources="inline", embed=True)


if __name__ == "__main__":
    main()
