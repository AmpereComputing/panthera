# Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import pandas as pd


def compute_ipc(df):
    date_config = df.configuration.str.split(".", expand=True)
    df.loc[:, "date"] = date_config.iloc[:, 0]
    df.loc[:, CONFIGURATION] = date_config.iloc[:, 1]

    ipc = df.loc[:, ["date", CONFIGURATION, SUITE, "sys.soc_0.core_0.ipc"]]
    ipc = ipc.set_index("date")

    create_csv(ipc, filename="study_list_ipc.csv")


compute_ipc(df)
