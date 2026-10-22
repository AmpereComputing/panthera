# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import evp


class ModeBW:
    def __init__(self, event, data, sum_name, count_name):
        self.cas_bw_sum = 0
        self.cas_bw_count = 0
        self.sum_name = sum_name
        self.count_name = count_name
        evp.on(event, lambda e: self.cas_bw(e, data))
        evp.collect(lambda t: self.collect())

    def cas_bw(self, event, data):
        self.cas_bw_sum += event.data[data]
        self.cas_bw_count += 1
        # print(f'{self.sum_name}: {self.cas_bw_sum}, {self.count_name}: {self.cas_bw_count}, data:{event.data[data]}')

    def collect(self):
        return {self.sum_name: self.cas_bw_sum, self.count_name: self.cas_bw_count}


ModeBW(
    "top.mc_0.rmm_read_cas_bandwidth",
    "top.mc_0.bandwidth",
    "mc_0_rmm_sum_read_bandwidth",
    "mc_0_rmm_sum_read_bandwidth_count",
)
ModeBW(
    "top.mc_0.wmm_read_cas_bandwidth",
    "top.mc_0.bandwidth",
    "mc_0_wmm_sum_read_bandwidth",
    "mc_0_wmm_sum_read_bandwidth_count",
)
ModeBW(
    "top.mc_0.w2r_read_cas_bandwidth",
    "top.mc_0.bandwidth",
    "mc_0_w2r_sum_read_bandwidth",
    "mc_0_w2r_sum_read_bandwidth_count",
)
ModeBW(
    "top.mc_0.r2w_read_cas_bandwidth",
    "top.mc_0.bandwidth",
    "mc_0_r2w_sum_read_bandwidth",
    "mc_0_r2w_sum_read_bandwidth_count",
)
ModeBW(
    "top.mc_0.rwmm_read_cas_bandwidth",
    "top.mc_0.bandwidth",
    "mc_0_rwmm_sum_read_bandwidth",
    "mc_0_rwmm_sum_read_bandwidth_count",
)

ModeBW(
    "top.mc_0.rmm_write_cas_bandwidth",
    "top.mc_0.bandwidth",
    "mc_0_rmm_sum_write_bandwidth",
    "mc_0_rmm_sum_write_bandwidth_count",
)
ModeBW(
    "top.mc_0.wmm_write_cas_bandwidth",
    "top.mc_0.bandwidth",
    "mc_0_wmm_sum_write_bandwidth",
    "mc_0_wmm_sum_write_bandwidth_count",
)
ModeBW(
    "top.mc_0.w2r_write_cas_bandwidth",
    "top.mc_0.bandwidth",
    "mc_0_w2r_sum_write_bandwidth",
    "mc_0_w2r_sum_write_bandwidth_count",
)
ModeBW(
    "top.mc_0.r2w_write_cas_bandwidth",
    "top.mc_0.bandwidth",
    "mc_0_r2w_sum_write_bandwidth",
    "mc_0_r2w_sum_write_bandwidth_count",
)
ModeBW(
    "top.mc_0.rwmm_write_cas_bandwidth",
    "top.mc_0.bandwidth",
    "mc_0_rwmm_sum_write_bandwidth",
    "mc_0_rwmm_sum_write_bandwidth_count",
)

ModeBW(
    "top.mc_0.rmm_read_cas_efficiency",
    "top.mc_0.cas_efficiency",
    "mc_0_rmm_sum_read_cas_efficiency",
    "mc_0_rmm_sum_read_cas_efficiency_count",
)
ModeBW(
    "top.mc_0.wmm_read_cas_efficiency",
    "top.mc_0.cas_efficiency",
    "mc_0_wmm_sum_read_cas_efficiency",
    "mc_0_wmm_sum_read_cas_efficiency_count",
)
ModeBW(
    "top.mc_0.w2r_read_cas_efficiency",
    "top.mc_0.cas_efficiency",
    "mc_0_w2r_sum_read_cas_efficiency",
    "mc_0_w2r_sum_read_cas_efficiency_count",
)
ModeBW(
    "top.mc_0.r2w_read_cas_efficiency",
    "top.mc_0.cas_efficiency",
    "mc_0_r2w_sum_read_cas_efficiency",
    "mc_0_r2w_sum_read_cas_efficiency_count",
)
ModeBW(
    "top.mc_0.rwmm_read_cas_efficiency",
    "top.mc_0.cas_efficiency",
    "mc_0_rwmm_sum_read_cas_efficiency",
    "mc_0_rwmm_sum_read_cas_efficiency_count",
)

ModeBW(
    "top.mc_0.rmm_write_cas_efficiency",
    "top.mc_0.cas_efficiency",
    "mc_0_rmm_sum_write_cas_efficiency",
    "mc_0_rmm_sum_write_cas_efficiency_count",
)
ModeBW(
    "top.mc_0.wmm_write_cas_efficiency",
    "top.mc_0.cas_efficiency",
    "mc_0_wmm_sum_write_cas_efficiency",
    "mc_0_wmm_sum_write_cas_efficiency_count",
)
ModeBW(
    "top.mc_0.w2r_write_cas_efficiency",
    "top.mc_0.cas_efficiency",
    "mc_0_w2r_sum_write_cas_efficiency",
    "mc_0_w2r_sum_write_cas_efficiency_count",
)
ModeBW(
    "top.mc_0.r2w_write_cas_efficiency",
    "top.mc_0.cas_efficiency",
    "mc_0_r2w_sum_write_cas_efficiency",
    "mc_0_r2w_sum_write_cas_efficiency_count",
)
ModeBW(
    "top.mc_0.rwmm_write_cas_efficiency",
    "top.mc_0.cas_efficiency",
    "mc_0_rwmm_sum_write_cas_efficiency",
    "mc_0_rwmm_sum_write_cas_efficiency_count",
)
