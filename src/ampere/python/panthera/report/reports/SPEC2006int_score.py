# Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Compute a SPEC2006int score from a study rollup.
"""

from panthera.report.helpers import *

frequency = 3000000000

scores_int = compute_spec_scores(df, frequency, "spec2006-gcc10_int_reference_data.csv")

create_csv(scores_int, "spec2006int_scores.csv")
create_plot(scores_int, "spec2006int_scores.svg", title="SPEC2006 Int Scores", y_label="Score")


print("\n==== SPEC2006 Int Scores ====")
print(scores_int)
