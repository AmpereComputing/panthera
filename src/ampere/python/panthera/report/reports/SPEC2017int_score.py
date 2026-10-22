# Copyright (c) 2019-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Compute a SPEC2017int score from a study rollup.
"""

import bokeh
import holoviews as hv
import hvplot.pandas  # noqa
import panel as pn
from panthera.report.helpers import *

hv.opts.defaults(
    hv.opts.Bars(
        width=800,
        height=400,
        xrotation=90,
        shared_axes=False,
    )
)

frequency = 3000000000
panel = pn.Column()

# The reference times data comes from the www.spec.org site.
# https://www.spec.org/cpu2017/results/res2017q2/cpu2017-20161026-00003.html
# The instruction counts were collected on a Skylark system.

scores_int = compute_spec_scores_subcomponent(df, frequency, "spec2017_int_subcomponent_reference_data.csv")

create_csv(scores_int, "spec2017int_scores.csv")
panel.append(scores_int.hvplot.bar().opts(title="SPEC2017 Int Scores", ylabel="Score"))

rate1 = read_data_file("spec2017-gcc10_int_rate_1.csv").pivot(index="Workload", columns="CPU", values="Score")
speedup = scores_int.div(rate1["Quicksilver Quadrant"], axis=0)
create_csv(speedup, "spec2017int_speedup.csv")
panel.append(speedup.hvplot.bar().opts(title="SPEC2017 Integer Score Speedup over Altra", ylabel="Speedup"))
print("\n==== SPEC2017 Int Scores ====")
print(scores_int)
print("\n==== SPEC2017 Int speedup over Quicksilver ====")
print(speedup)

html_file = make_filepath("spec2017int_report.html")
logging.info(f"Saving {html_file}")
panel.save(html_file, title="SPEC2017INT Report", resources=bokeh.resources.INLINE)
