# Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import pandas as pd
from scipy import stats


def compute_mean_ipc(df):
    date_config = df.configuration.str.split(".", expand=True)
    df.loc[:, "date"] = date_config.iloc[:, 0]
    df.loc[:, CONFIGURATION] = date_config.iloc[:, 1]

    ipc = df.loc[:, ["date", CONFIGURATION, WORKLOAD, "sys.soc_0.core_0.ipc"]]
    ipc.rename(columns={"sys.soc_0.core_0.ipc": "ipc"}, inplace=True)
    mean = ipc.groupby(["date", CONFIGURATION], as_index=True).ipc.apply(stats.gmean)
    mean = mean.to_frame()
    mean.rename(columns={"ipc": "sys.soc_0.core_0.ipc"}, inplace=True)

    create_csv(mean, filename="study_list_mean_ipc.csv")


compute_mean_ipc(df)
