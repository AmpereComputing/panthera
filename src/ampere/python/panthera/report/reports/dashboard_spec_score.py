# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Generate CSV files for Weekly Dashboard"""

import pandas as pd

frequency = 3000000000


def compute_scores(df, suite, config, compiler):
    df = df[df[CONFIGURATION].str.endswith(config)]
    if compiler == "":
        # assumption is that suite is int or fp
        if suite == "int":
            ref_data = "spec2017_int_subcomponent_reference_data.csv"
            scores = compute_spec_scores_subcomponent(df, frequency, ref_data)
        else:
            ref_data = "spec2017_fp_reference_data.csv"
            scores = compute_spec_scores(df, frequency, ref_data)
    else:
        ref_data = f"spec2017-{compiler}_{suite}_reference_data.csv"
        scores = compute_spec_scores(df, frequency, ref_data)

    scores_t = scores.T.reset_index()
    date_config = scores_t.configuration.str.split(".", expand=True)

    scores_t.loc[:, "date"] = date_config.iloc[:, 0]
    scores_t = scores_t.set_index("date")
    return scores_t.drop(CONFIGURATION, axis=1)


specs = [
    ("ISPEC17", "int", "SPEC2017INT"),
    ("FSPEC17", "fp", "SPEC2017FP"),
]
for suite, spec_type, name in specs:
    spec_score_list = []
    configs = set([c.split(".")[-1] for c in df[CONFIGURATION].unique()])
    for config in configs:
        if not (df[df[CONFIGURATION].str.endswith(config)][SUITE] == suite).all():
            continue

        scores = compute_scores(df, "int", config, "")
        scores.loc[:, CONFIGURATION] = config
        scores.loc[:, "product"] = re.sub(r"-.*$", "", config)
        scores.loc[:, "rate"] = re.sub(r"^.*?-Rate-", "", config).removesuffix("-gcc10")
        spec_score_list.append(scores)

    if spec_score_list:
        spec_scores = pd.concat(spec_score_list)
        create_csv(spec_scores, filename=f"{name}_scores.csv")
