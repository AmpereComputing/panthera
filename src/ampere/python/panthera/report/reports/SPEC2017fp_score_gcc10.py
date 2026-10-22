# Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Compute a SPEC2017fp score from a study rollup.
"""

frequency = 3000000000

scores_fp = compute_spec_scores(df, frequency, "spec2017-gcc10_fp_reference_data.csv")

create_csv(scores_fp, "spec2017fp_scores_gcc10.csv")
create_plot(scores_fp, "spec2017fp_scores_gcc10.svg", title="SPEC2017 FP Scores GCC 10", y_label="Score")

print("\n==== SPEC2017 FP Scores ====")
print(scores_fp)
