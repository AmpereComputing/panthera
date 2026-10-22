# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Compute a SPEC2017fp score from a study rollup.
"""

frequency = 3000000000

scores_fp = compute_spec_scores(df, frequency, "spec2017_fp_reference_data.csv")

create_csv(scores_fp, "spec2017fp_scores.csv")
create_plot(scores_fp, "spec2017fp_scores.svg", title="SPEC2017 FP Scores", y_label="Score")

print("\n==== SPEC2017 FP Scores ====")
print(scores_fp)
