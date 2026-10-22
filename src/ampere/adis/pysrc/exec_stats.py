#
# Copyright (c) 2022 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
from __future__ import annotations
import collections

class execution_stats_t:
    def __init__(self):
        # masks keep track of how many 32b masks we are using to divide up
        # the encoding space.
        self.masks = collections.defaultdict(int)
        self.no_matching_required_bits = 0
        self.could_not_find_hash_function = 0
        self.max_time_to_compute_hash = 0
        self.disjoint_values_in_forbid_masks = 0
    def print(self):
        print(f"# of gmasks = {len(self.masks)}")
        for k, v in self.masks.items():
            print(f'{k:#010x} : {v}')
        print(f'Cases with no matching required bits:     {self.no_matching_required_bits}')
        print(f'Cases when couldn not find hash function: {self.could_not_find_hash_function}')
        print(f'Max time to compute a hash function:      {self.max_time_to_compute_hash:04f}')
        print(f'Disjoint values in forbid masks:          {self.disjoint_values_in_forbid_masks}')

