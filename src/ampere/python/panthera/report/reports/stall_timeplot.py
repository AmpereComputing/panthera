# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Uop allocation stall over time"""

stall_metrics = ["sys.soc_0.core_0.idr.backend_stall"] + list(
    df.filter(regex=r"^sys\.soc_0\.core_0\.idr\.backend_stall_cause\..*").columns
)

backend_stall = df[stall_metrics]
backend_stall = backend_stall.rename(columns=lambda c: c.split(":")[-1])
backend_stall.index = df["stop_time"]

create_plot(backend_stall, "stall.svg", kind="line", title="Allocate Stall")
print(backend_stall.describe())
