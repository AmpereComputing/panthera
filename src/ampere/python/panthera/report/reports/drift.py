# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Performance Drift"""

import pandas as pd

frequency = 3000000000


def speedup(df, base, value):
    grouped = df.groupby(CONFIGURATION).mean()
    ipc = grouped["sys.soc_0.core_0.ipc"].reset_index()

    date_config = ipc.configuration.str.split(".", expand=True)

    ipc.loc[:, "date"] = date_config.iloc[:, 0]
    ipc.loc[:, CONFIGURATION] = date_config.iloc[:, 1]

    tidy_ipc = ipc.pivot(index="date", columns=CONFIGURATION, values="sys.soc_0.core_0.ipc")

    sup = tidy_ipc.loc[:, [base, value]]
    sup.loc[:, f"{value} speedup"] = sup[value] / sup[base]
    create_csv(sup, filename=f"{value}_speedup.csv")
    create_plot(
        sup.loc[:, f"{value} speedup"],
        filename=f"{value}_speedup.svg",
        title=f"Speedup of {value} over {base}",
        y_label=f"Speedup over {base}",
        kind="line",
        marker="o",
    )
    print(sup)


def compute_drift(df, suite, product, compiler, rate):
    rate1 = read_data_file(f"spec2017{compiler}_{suite}_rate_{rate}.csv").pivot(
        index="Workload", columns="CPU", values="Score"
    )

    if compiler == "":
        if suite == "int":
            df = df[df.configuration.str.endswith(f"{product}-Rate-{rate}{compiler}")]
        else:
            df = df[df.configuration.str.endswith(f"{product}-FP-Rate-{rate}{compiler}")]
    else:
        df = df[df.configuration.str.endswith(f"{product}-Rate-{rate}{compiler}")]

    scores = compute_spec_scores(df, frequency, f"spec2017{compiler}_{suite}_reference_data.csv")

    speedup = scores.div(rate1["Quicksilver"], axis=0)

    others = ["Quicksilver"]

    most_recent = scores.iloc[:, -1]
    furiosa = "Furiosa " + most_recent.name

    comparison = rate1.loc[:, others]
    comparison.loc[:, furiosa] = most_recent

    create_plot(
        comparison,
        filename=f"spec2017_{suite}{compiler}_rate_{rate}_comparison.svg",
        title=f"SPEC2017 {suite} {compiler} rate {rate} Score Comparison",
        y_label="Score",
    )

    geomean = speedup.loc["Geomean"]

    create_plot(
        geomean,
        filename=f"spec2017_{suite}{compiler}_rate_{rate}_mean_speedup.svg",
        title=f"SPEC2017 {suite} Geomean Score Speedup",
        y_label="Speedup over Quicksilver",
        rot=45,
        kind="line",
        marker="o",
    )
    create_csv(geomean, filename=f"spec2017_{suite}{compiler}_rate_{rate}_mean_speedup.csv")
    geomean.index = geomean.index.str.replace("\..*", "")
    return geomean


configs = [
    ("fp", "Siryn", "-gcc10", 1),
    ("fp", "Banshee", "-gcc10", 1),
    ("int", "Siryn", "-gcc10", 1),
    ("int", "Banshee", "-gcc10", 1),
    ("int", "Siryn", "-gcc10", "N"),
    ("int", "Banshee", "-gcc10", "N"),
]

speedup(df, "Siryn", "Banshee")

summary = {}
for config in configs:
    name = f"{config[1]} {config[0]}{config[2]} rate {config[3]}"
    summary[name] = compute_drift(df, *config)

all = pd.DataFrame(summary)
create_csv(all, filename="weekly_drift.csv")
print(all)
