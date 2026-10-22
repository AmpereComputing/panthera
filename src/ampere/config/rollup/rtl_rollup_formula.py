# Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

core = metric.sys.soc_0.core_0

# Machine width
machine_width = 4

# Core metrics
metric.cycles = core.pmu.cpu_cycles
core.ipc = core.pmu.inst_retired / metric.cycles

# Ops metrics
core.ops_retired = core.pmu.op_retired / core.pmu.op_spec
core.ops_lost = 1 - core.ops_retired
core.cpu_utilization = core.pmu.op_retired / (metric.cycles * machine_width)
core.cpu_lost = 1 - core.cpu_utilization

# Branch prediction metrics
core.branches = core.pmu.br_retired
core.mispredicts = core.pmu.br_mis_pred_retired
core.branch.mispredict.pki = 1000 * (core.mispredicts / core.pmu.inst_retired)
core.branch.mispredict.ratio = core.mispredicts / core.branches

# ICF caches
core.icf.itlb.hit_rate = 1 - (core.pmu.l1i_tlb_refill / core.pmu.l1i_tlb)
core.icf.cache.hit_rate = 1 - (core.pmu.l1i_cache_refill / core.pmu.l1i_cache)
core.icf.cache.long.hit_rate = 1 - (core.pmu.l1i_cache_lmiss / core.pmu.l1i_cache)
core.itlb.mpki = 1000 * core.pmu.l1i_tlb_refill / core.pmu.inst_retired
core.icache.mpki = 1000 * core.pmu.l1i_cache_refill / core.pmu.inst_retired
core.icache.lmpki = 1000 * core.pmu.l1i_cache_lmiss / core.pmu.inst_retired

# LSU caches
core.lsu.dl1_tlb.hit_rate = 1 - (core.pmu.l1d_tlb_refill / core.pmu.l1d_tlb)
core.lsu.l1d_cache.hit_rate = 1 - (core.pmu.l1d_cache_refill / core.pmu.l1d_cache)
core.lsu.l1d_cache.read.hit_rate = 1 - (core.pmu.l1d_cache_lmiss_rd / core.pmu.l1d_cache_rd)
core.dtlb.mpki = 1000 * core.pmu.l1d_tlb_refill / core.pmu.inst_retired
core.dcache.mpki = 1000 * core.pmu.l1d_cache_refill / core.pmu.inst_retired
core.dcache.lmpki = 1000 * core.pmu.l1d_cache_lmiss_rd / core.pmu.inst_retired

# L2 caches
core.l2cache.hit_rate = 1 - (core.pmu.l2d_cache_refill / core.pmu.l2d_cache)
core.l2cache.read.hit_rate = 1 - (core.pmu.l2d_cache_lmiss_rd / core.pmu.l2d_cache_rd)
core.l2cache.mpki = 1000 * core.pmu.l2d_cache_refill / core.pmu.inst_retired
core.l2cache.lmpki = 1000 * core.pmu.l2d_cache_lmiss_rd / core.pmu.inst_retired

# MMU TLBs
core.immu.miss_rate = core.pmu.l2i_tlb_refill / core.pmu.l2i_tlb
core.immu.hit_rate = 1 - core.immu.miss_rate
core.immu.mpki = 1000 * core.pmu.l2i_tlb_refill / core.pmu.inst_retired

core.dmmu.miss_rate = core.pmu.l2d_tlb_refill / core.pmu.l2d_tlb
core.dmmu.hit_rate = 1 - core.dmmu.miss_rate
core.dmmu.mpki = 1000 * core.pmu.l2d_tlb_refill / core.pmu.inst_retired

# TLB walks
core.dtlb_walk_rate = core.pmu.dtlb_walk / core.pmu.l1d_tlb
core.itlb_walk_rate = core.pmu.itlb_walk / core.pmu.l1i_tlb

# Op Mix
core.load_mix = core.pmu.ld_spec / core.pmu.op_spec
core.store_mix = core.pmu.st_spec / core.pmu.op_spec
core.load_store_mix = core.pmu.ldst_spec / core.pmu.op_spec
core.integer_mix = core.pmu.dp_spec / core.pmu.op_spec
core.ase_mix = core.pmu.ase_spec / core.pmu.op_spec
core.vfp_mix = core.pmu.vfp_spec / core.pmu.op_spec
core.pc_write_mix = core.pmu.pc_write_spec / core.pmu.op_spec
core.crypto_mix = core.pmu.crypto_spec / core.pmu.op_spec

# Stalls
core.stalls = core.pmu.stall
core.stalls_pki = 1000 * core.pmu.stall / core.pmu.inst_retired
core.stalls_frontend_pki = 1000 * core.pmu.stall_frontend / core.pmu.inst_retired
core.stalls_backend_pki = 1000 * core.pmu.stall_backend / core.pmu.inst_retired
core.stall_rate = core.pmu.stall / metric.cycles
core.stall_frontend_rate = core.pmu.stall_frontend / metric.cycles
core.stall_backend_rate = core.pmu.stall_backend / metric.cycles
core.stall_backend_mem_rate = core.pmu.stall_backend_mem / metric.cycles

# Top Down Accounting metrics
tda = metric.tda
ixu_width = 6
fsu_width = 3

tda.slots = core.pmu.cpu_cycles * machine_width
tda.l1.retired = core.pmu.op_retired / tda.slots
tda.l1.lost = ((core.pmu.op_spec - core.pmu.op_cleaved_spec) - core.pmu.op_retired) / tda.slots
tda.l1.frontend = core.pmu.stall_slot_frontend / tda.slots
tda.l1.backend = core.pmu.stall_slot_backend / tda.slots

tda.l2.frontend_latency = (core.pmu.stall_frontend * machine_width) / tda.slots
tda.l2.frontend_bandwidth = (core.pmu.stall_slot_frontend - (core.pmu.stall_frontend * machine_width)) / tda.slots
tda.l2.backend_memory = (
    (core.pmu.stall_backend_tlb + core.pmu.stall_backend_l1d + core.pmu.stall_backend_mem) * machine_width
) / tda.slots
tda.l2.backend_core = (core.pmu.stall_backend - core.pmu.stall_backend_membound) / core.pmu.cpu_cycles
tda.l2.retired_pipe_utilization = (core.pmu.ixu_num_uops_issued + core.pmu.fsu_issued) / (
    core.pmu.cpu_cycles * (ixu_width + fsu_width)
)
tda.l2.lost_branch_mispredict = (core.pmu.br_mis_pred_retired / core.pmu.gpc_flush) * tda.l1.lost
tda.l2.lost_other_clear = tda.l1.lost - tda.l2.lost_branch_mispredict

tda.l3.frontend_latency_l1i = (
    core.pmu.stall_frontend_membound
    - core.pmu.stall_frontend_tlb
    - core.pmu.stall_frontend_l1i
    - core.pmu.stall_frontend_mem
) / core.pmu.cpu_cycles
tda.l3.frontend_latency_l2 = rollup.per_cycle(core.pmu.stall_frontend_l1i, core)
tda.l3.frontend_latency_mem = rollup.per_cycle(core.pmu.stall_frontend_mem, core)
tda.l3.frontend_latency_flow = rollup.per_cycle(core.pmu.stall_frontend_flow, core)
tda.l3.frontend_latency_itlb = rollup.per_cycle(core.pmu.stall_frontend_tlb, core)
tda.l3.frontend_latency_recovery = rollup.per_cycle(core.pmu.stall_frontend_flush, core)
tda.l3.backend_memory_l1d = (
    core.pmu.stall_backend_membound
    - core.pmu.stall_backend_tlb
    - core.pmu.stall_backend_l1d
    - core.pmu.stall_backend_mem
) / core.pmu.cpu_cycles
tda.l3.backend_memory_dtlb = rollup.per_cycle(core.pmu.stall_backend_tlb, core)
tda.l3.backend_memory_l2 = rollup.per_cycle(core.pmu.stall_backend_l1d, core)
tda.l3.backend_memory_mem = rollup.per_cycle(core.pmu.stall_backend_mem, core)
tda.l3.backend_memory_atomic = rollup.per_cycle(core.pmu.stall_backend_atomic, core)
tda.l3.backend_core_serialize = rollup.per_cycle(core.pmu.idr_stall_drain, core)
tda.l3.backend_core_schedulers = (core.pmu.idr_stall_ixu_sched + core.pmu.idr_stall_fsu_sched) / core.pmu.cpu_cycles
tda.l3.backend_core_other = (
    core.pmu.stall_backend_resource - core.pmu.idr_stall_ixu_sched - core.pmu.idr_stall_fsu_sched
) / core.pmu.cpu_cycles
tda.l3.retired_pipe_utilization_ixu = core.pmu.ixu_num_uops_issued / (core.pmu.cpu_cycles * ixu_width)
tda.l3.retired_pipe_utilization_fsu = core.pmu.fsu_issued / (core.pmu.cpu_cycles * fsu_width)

tda.l4.backend_core_resource_rob = core.pmu.idr_stall_rob_id / core.pmu.cpu_cycles
tda.l4.backend_core_resource_ixu = core.pmu.idr_stall_ixu_sched / core.pmu.cpu_cycles
tda.l4.backend_core_resource_fsu = core.pmu.idr_stall_fsu_sched / core.pmu.cpu_cycles
tda.l4.backend_core_resource_bob = core.pmu.idr_stall_bob_id / core.pmu.cpu_cycles
tda.l4.backend_core_resource_lob = core.pmu.idr_stall_lob_id / core.pmu.cpu_cycles
tda.l4.backend_core_resource_sob = core.pmu.idr_stall_sob_id / core.pmu.cpu_cycles
tda.l4.backend_core_resource_rename = rollup.per_cycle(core.pmu.stall_backend_rename, core)
tda.l4.backend_core_exec_units = rollup.per_cycle(core.pmu.stall_backend_busy, core)
tda.l4.backend_core_dependancies = rollup.per_cycle(core.pmu.stall_backend_ilock, core)
