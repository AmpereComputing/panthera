# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Simple transpose of csv"""

import re

import pandas as pd
from panthera.report.helpers import *

## Transpose a tidy csv
## to make it easier to handle with less, grep, or tkdiff.

## The regular read of df would not preserve floating point exactly so it will
## be the same if the file is transposed back and forth. So reread the csv
## into a private version of df.
global input_file
raw = pd.read_csv(input_file, dtype="string", header=None, low_memory=False)
## This header stuff handles the case where there are multiple columns with the same name
## (as can happen if you transpose a summary file back and forth)
header = raw.iloc[0].tolist()
_df = raw.iloc[1:].copy()
_df.columns = header
_df.reset_index(drop=True, inplace=True)

dfT = _df.T.reset_index()

output_file = re.sub(r"\.csv$", "", input_file)
output_file = output_file + ".T.csv"
dfT.to_csv(output_file, index=False, header=False)

print(f"Transposed into {output_file}")
