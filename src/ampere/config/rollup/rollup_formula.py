# Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

soc = metric.sys.soc_0
core = soc.core_0

# TODO it should one day be assumed that `core.machine_width` is always defined
# via event stream parameters, meaning this if/else block could be replaced
# with only:
#    machine_width = core.machine_width
# This was not done for now to allow `panthera rollup` to continue working with
# studies run prior to the new parameter-embedding
if not core.machine_width.empty:
    machine_width = core.machine_width
elif not core.core_width.empty:
    machine_width = core.core_width
else:
    machine_width = 4

# PMUs which are equivalent to other events, or sums of other events
core.pmu.cpu_cycles = core.gpc.cycle
core.pmu.inst_retired = core.gpc.instruction_commit
core.pmu.mem_access = core.pmu.mem_access_rd + core.pmu.mem_access_wr
if not core.pmu.mem_access_rd_percyc.empty or not core.pmu.mem_access_wr_percyc.empty:
    core.pmu.mem_access_percyc = core.pmu.mem_access_rd_percyc + core.pmu.mem_access_wr_percyc
core.pmu.l1d_tlb_rw = core.pmu.l1d_tlb_rd + core.pmu.l1d_tlb_wr
core.pmu.l1d_tlb_refill = core.pmu.l1d_tlb_refill_rd + core.pmu.l1d_tlb_refill_wr
core.pmu.l1d_cache_refill_rw = core.pmu.l1d_cache_refill_rd
if not core.pmu.l1d_cache_refill_prfm.empty:
    core.pmu.l1d_cache_refill = core.pmu.l1d_cache_refill_rw + core.pmu.l1d_cache_refill_prfm
else:
    core.pmu.l1d_cache_refill = core.pmu.l1d_cache_refill_rw
core.pmu.l1d_cache_rw = core.pmu.l1d_cache_rd + core.pmu.l1d_cache_wr
if not core.pmu.l1d_cache_prfm.empty:
    core.pmu.l1d_cache = core.pmu.l1d_cache_rw + core.pmu.l1d_cache_prfm
else:
    core.pmu.l1d_cache = core.pmu.l1d_cache_rw
core.pmu.ldst_align_lat = core.pmu.ld_align_lat + core.pmu.st_align_lat
core.pmu.lsu_st_retire_stall = core.lsu.uop_st_retire_stall
core.pmu.loop_mode_cycle_cntr = core.bpu.looping
core.pmu.stall_backend = core.idr.backend_stall + core.pmu.idr_stall_drain
core.pmu.bpu_btc_clear = core.pmu.bpu_btc_clear_bad_prediction + core.pmu.bpu_btc_clear_btb_miss
core.pmu.icf_early_mis_pred = core.pmu.bpu_btc_clear

# Core metrics
metric.cycles = core.pmu.cpu_cycles
metric.seconds = (metric.stop_time - metric.start_time) / 1e12
core.ipc = rollup.per_cycle(core.pmu.inst_retired, core)
core.ips = core.pmu.inst_retired / metric.seconds
core.upc = rollup.per_cycle(core.gpc.uop_commit, core)
if not core.gpc.cycle_EL0.empty:
    core.ipc_el0 = core.gpc.instruction_commit_EL0 / core.gpc.cycle_EL0
if not core.gpc.cycle_EL1.empty:
    core.ipc_el1 = core.gpc.instruction_commit_EL1 / core.gpc.cycle_EL1
if not core.gpc.cycle_EL2.empty:
    core.ipc_el2 = core.gpc.instruction_commit_EL2 / core.gpc.cycle_EL2
if not core.gpc.cycle_EL3.empty:
    core.ipc_el3 = core.gpc.instruction_commit_EL3 / core.gpc.cycle_EL3

if not core.gpc.cycle_EL0.empty:
    core.upc_el0 = core.gpc.uop_commit_EL0 / core.gpc.cycle_EL0
if not core.gpc.cycle_EL1.empty:
    core.upc_el1 = core.gpc.uop_commit_EL1 / core.gpc.cycle_EL1
if not core.gpc.cycle_EL2.empty:
    core.upc_el2 = core.gpc.uop_commit_EL2 / core.gpc.cycle_EL2
if not core.gpc.cycle_EL3.empty:
    core.upc_el3 = core.gpc.uop_commit_EL3 / core.gpc.cycle_EL3

if not core.gpc.cycle_warmup_EL0.empty:
    core.ipc_warmup_el0 = core.gpc.instruction_commit_warmup_EL0 / core.gpc.cycle_warmup_EL0
if not core.gpc.cycle_warmup_EL1.empty:
    core.ipc_warmup_el1 = core.gpc.instruction_commit_warmup_EL1 / core.gpc.cycle_warmup_EL1
if not core.gpc.cycle_warmup_EL2.empty:
    core.ipc_warmup_el2 = core.gpc.instruction_commit_warmup_EL2 / core.gpc.cycle_warmup_EL2
if not core.gpc.cycle_warmup_EL3.empty:
    core.ipc_warmup_el3 = core.gpc.instruction_commit_warmup_EL3 / core.gpc.cycle_warmup_EL3

if not core.gpc.cycle_warmup_EL0.empty:
    core.upc_warmup_el0 = core.gpc.uop_commit_warmup_EL0 / core.gpc.cycle_warmup_EL0
if not core.gpc.cycle_warmup_EL1.empty:
    core.upc_warmup_el1 = core.gpc.uop_commit_warmup_EL1 / core.gpc.cycle_warmup_EL1
if not core.gpc.cycle_warmup_EL2.empty:
    core.upc_warmup_el2 = core.gpc.uop_commit_warmup_EL2 / core.gpc.cycle_warmup_EL2
if not core.gpc.cycle_warmup_EL3.empty:
    core.upc_warmup_el3 = core.gpc.uop_commit_warmup_EL3 / core.gpc.cycle_warmup_EL3

if not core.gpc.cycle_measurement_EL0.empty:
    core.ipc_measurement_el0 = core.gpc.instruction_commit_measurement_EL0 / core.gpc.cycle_measurement_EL0
if not core.gpc.cycle_measurement_EL1.empty:
    core.ipc_measurement_el1 = core.gpc.instruction_commit_measurement_EL1 / core.gpc.cycle_measurement_EL1
if not core.gpc.cycle_measurement_EL2.empty:
    core.ipc_measurement_el2 = core.gpc.instruction_commit_measurement_EL2 / core.gpc.cycle_measurement_EL2
if not core.gpc.cycle_measurement_EL3.empty:
    core.ipc_measurement_el3 = core.gpc.instruction_commit_measurement_EL3 / core.gpc.cycle_measurement_EL3

if not core.gpc.cycle_measurement_EL0.empty:
    core.upc_measurement_el0 = core.gpc.uop_commit_measurement_EL0 / core.gpc.cycle_measurement_EL0
if not core.gpc.cycle_measurement_EL1.empty:
    core.upc_measurement_el1 = core.gpc.uop_commit_measurement_EL1 / core.gpc.cycle_measurement_EL1
if not core.gpc.cycle_measurement_EL2.empty:
    core.upc_measurement_el2 = core.gpc.uop_commit_measurement_EL2 / core.gpc.cycle_measurement_EL2
if not core.gpc.cycle_measurement_EL3.empty:
    core.upc_measurement_el3 = core.gpc.uop_commit_measurement_EL3 / core.gpc.cycle_measurement_EL3

# Ops metrics
core.ops_retired = core.pmu.op_retired / core.pmu.op_spec
core.ops_lost = 1 - core.ops_retired
core.cpu_utilization = core.pmu.op_retired / (metric.cycles * machine_width)
core.cpu_lost = 1 - core.cpu_utilization
core.rob_entries_committed = core.gpc.uop_commit - core.gpc.cleaved_uop_commit
core.instructions_per_rob_entry = core.gpc.instruction_commit / core.rob_entries_committed

# Branch prediction metrics
core.branches = core.pmu.br_retired
core.mispredicts = core.pmu.br_mis_pred_retired
core.branch.mispredict.pki = rollup.pki(core.mispredicts, core)
core.branch.mispredict.ratio = core.mispredicts / core.branches

# ICF caches
core.icf.itlb.hit_rate = 1 - (core.pmu.l1i_tlb_refill / core.pmu.l1i_tlb)
core.icf.cache.hit_rate = 1 - (core.pmu.l1i_cache_refill / core.pmu.l1i_cache)
core.icf.cache.long.hit_rate = 1 - (core.pmu.l1i_cache_lmiss / core.pmu.l1i_cache)
core.itlb.mpki = rollup.pki(core.pmu.l1i_tlb_refill, core)
core.icache.mpki = rollup.pki(core.pmu.l1i_cache_refill, core)
core.icache.lmpki = rollup.pki(core.pmu.l1i_cache_lmiss, core)

# LSU caches
core.lsu.dl1_tlb.hit_rate = 1 - (core.pmu.l1d_tlb_refill / core.pmu.l1d_tlb)
core.lsu.l1d_cache.hit_rate = 1 - (core.pmu.l1d_cache_refill / core.pmu.l1d_cache)
core.lsu.l1d_cache.read.hit_rate = 1 - (core.pmu.l1d_cache_lmiss_rd / core.pmu.l1d_cache_rd)
core.lsu.l1d_cache.evict = core.lsu.inval_modified
core.dtlb.mpki = rollup.pki(core.pmu.l1d_tlb_refill, core)
core.dcache.mpki = rollup.pki(core.pmu.l1d_cache_refill, core)
core.dcache.lmpki = rollup.pki(core.pmu.l1d_cache_lmiss_rd, core)

# L2 caches
core.l2cache.hit_rate = 1 - (core.pmu.l2d_cache_refill / core.pmu.l2d_cache)
core.l2cache.read.hit_rate = 1 - (core.pmu.l2d_cache_refill_rd / core.pmu.l2d_cache_rd)
core.l2cache.mpki = rollup.pki(core.pmu.l2d_cache_refill, core)
core.l2cache.lmpki = rollup.pki(core.pmu.l2d_cache_lmiss_rd, core)

# L2 prefetcher
core.l2c.prefetcher.bop.drop = (
    core.l2c.prefetcher.drop
    - core.l2c.prefetcher.sbop_drop
    - core.l2c.prefetcher.spatial.drop
    - core.l2c.prefetcher.nextline.drop
)
core.l2c.prefetcher.bop.fill = (
    core.l2c.prefetcher.fill
    - core.l2c.prefetcher.sbop_fill
    - core.l2c.prefetcher.spatial.fill
    - core.l2c.prefetcher.nextline.fill
)
core.l2c.prefetcher.bop.hit = (
    core.l2c.prefetcher.hit
    - core.l2c.prefetcher.sbop_hit
    - core.l2c.prefetcher.spatial.hit
    - core.l2c.prefetcher.nextline.hit
)
core.l2c.prefetcher.bop.launch = (
    core.l2c.prefetcher.launch
    - core.l2c.prefetcher.sbop_launch
    - core.l2c.prefetcher.spatial.launch
    - core.l2c.prefetcher.nextline.launch
)

core.l2c.prefetcher.bop.accuracy = rollup.l2_prefetcher.accuracy(
    core.l2c.prefetcher.bop.hit, core.l2c.prefetcher.bop.launch, core.l2c.prefetcher.bop.drop
)

core.l2c.prefetcher.accuracy = rollup.l2_prefetcher.accuracy(
    core.l2c.prefetcher.hit, core.l2c.prefetcher.launch, core.l2c.prefetcher.drop
)

core.l2c.prefetcher.sbop.accuracy = rollup.l2_prefetcher.accuracy(
    core.l2c.prefetcher.sbop_hit, core.l2c.prefetcher.sbop_launch, core.l2c.prefetcher.sbop_drop
)

core.l2c.prefetcher.nextline.accuracy = rollup.l2_prefetcher.accuracy(
    core.l2c.prefetcher.nextline.hit, core.l2c.prefetcher.nextline.launch, core.l2c.prefetcher.nextline.drop
)

core.l2c.prefetcher.spatial.accuracy = rollup.l2_prefetcher.accuracy(
    core.l2c.prefetcher.spatial.hit, core.l2c.prefetcher.spatial.launch, core.l2c.prefetcher.spatial.drop
)

if not core.pmu.l2c_data_refill.empty:
    core.l2c.prefetcher.coverage = core.pmu.l2c_cache_fhit_hwprf / (
        core.pmu.l2c_data_refill + core.pmu.l2c_cache_fhit_hwprf - core.pmu.l2d_cache_refill_wr
    )
    core.l2c.prefetcher.activity = core.pmu.l2_prefetch_refill / (
        core.pmu.l2_prefetch_refill + core.pmu.l2c_data_refill
    )
else:
    core.l2c.prefetcher.coverage = 0
    core.l2c.prefetcher.activity = 0

# L2 stashes
core.l2c.stash_reject_ratio = core.l2c.stash_reject / core.l2c.recv_stash

# L1 PCP prefetcher
if not core.lsu.prefetcher.pcp.launch.empty:
    core.lsu.prefetcher.pcp.demand_hit_pcp = (
        core.lsu.prefetcher.pcp.demand_hit_cacheline + core.lsu.prefetcher.pcp.demand_hit_ofb
    )
    core.lsu.prefetcher.pcp.accuracy = rollup.l1_prefetcher.accuracy(
        core.lsu.prefetcher.pcp.demand_hit_pcp, 0, core.lsu.prefetcher.pcp.launch
    )

# L1 GRETCH prefetcher
if not core.lsu.prefetcher.gretch.launch.empty:
    core.lsu.prefetcher.gretch.accuracy = rollup.l1_prefetcher.accuracy(
        core.lsu.prefetcher.gretch.demand_hit_cacheline,
        core.lsu.prefetcher.gretch.demand_hit_ofb,
        core.lsu.prefetcher.gretch.launch,
    )

# L1 BOP prefetcher
if not core.lsu.prefetcher.l1bop.launch.empty:
    core.lsu.prefetcher.l1bop.accuracy = rollup.l1_prefetcher.accuracy(
        core.lsu.prefetcher.l1bop.demand_hit_cacheline,
        core.lsu.prefetcher.l1bop.demand_hit_ofb,
        core.lsu.prefetcher.l1bop.launch,
    )

# MMU TLBs
core.immu.miss_rate = core.pmu.l2i_tlb_refill / core.pmu.l2i_tlb
core.immu.hit_rate = 1 - core.immu.miss_rate
core.immu.mpki = rollup.pki(core.pmu.l2i_tlb_refill, core)

core.dmmu.miss_rate = core.pmu.l2d_tlb_refill / core.pmu.l2d_tlb
core.dmmu.hit_rate = 1 - core.dmmu.miss_rate
core.dmmu.mpki = rollup.pki(core.pmu.l2d_tlb_refill, core)

# TLB walks
core.dtlb_walk_rate = core.pmu.dtlb_walk / core.pmu.l1d_tlb
core.itlb_walk_rate = core.pmu.itlb_walk / core.pmu.l1i_tlb

# SLC (mesh cache) -- adding one to avoid divide by zero
soc.slc.miss_rate = soc.slc.miss / (soc.slc.access + 1)
soc.slc.hit_rate = soc.slc.hit / (soc.slc.access + 1)
soc.slc.mpki = rollup.pki(soc.slc.miss, core)

# Op Mix
core.load_mix = core.pmu.ld_spec / core.pmu.op_spec
core.store_mix = core.pmu.st_spec / core.pmu.op_spec
core.load_store_mix = core.pmu.ldst_spec / core.pmu.op_spec
core.integer_mix = core.pmu.dp_spec / core.pmu.op_spec
core.ase_mix = core.pmu.ase_spec / core.pmu.op_spec
core.vfp_mix = core.pmu.vfp_spec / core.pmu.op_spec
core.pc_write_mix = core.pmu.pc_write_spec / core.pmu.op_spec
core.crypto_mix = core.pmu.crypto_spec / core.pmu.op_spec
core.pmu.ase_sve_fp_fma_mul_spec = core.pmu.pem_ase_sve_fp_fma_mul_spec
core.pmu.ase_sve_fp_dot_mul_spec = core.pmu.pem_ase_sve_fp_dot_mul_spec
core.pmu.ase_sve_int_mul_mmla_dot_spec = core.pmu.pem_ase_sve_int_mul_mmla_dot_spec

# Compute frontend (FE) "IPC", a notion of the average number of instructions
# the front end is delivering to the back of the machine irrespective of back
# end bottlenecks. "Rightpath" frontend IPC is a variant which reports the
# average number of right-path instructions the front end is delivering to the
# back of the machine - it reports 0 if the frontend is delivering the wrong
# instructions.
core.fe_ipc = (
    rollup.sum_by_regex(core.idr, r"fe_bw/sys.soc_0.core_0.idr.iq_occupancy:\d", weight_by_factor=True) / core.idr.fe_bw
)
core.fe_rightpath_ipc = (
    rollup.sum_by_regex(core.idr, r"fe_bw/sys.soc_0.core_0.idr.iq_rightpath_occupancy:\d", weight_by_factor=True)
    / core.idr.fe_bw
)

# Detailed branch misprediction breakdown

core.branch.mispredict_conditional.pki = rollup.pki(core.branch_mispredict_conditional, core)
core.branch.mispredict_indirect.pki = rollup.pki(
    (core.branch_mispredict_call_indirect + core.branch_mispredict_unconditional_indirect), core
)
core.branch.mispredict_return.pki = rollup.pki(core.branch_mispredict_return, core)

core.branch.mispredict_btb_miss.pki = rollup.pki(core.branch_mispredict_btb_miss, core)

# Loop predictor stats
core.cycles_looping = rollup.per_cycle(core.icf.loop_replay, core)
core.instructions_looping = core.gpc.looped_instruction_commit / core.gpc.instruction_commit

# Store forward breakdown
core.raw_scrub.pki = rollup.pki(core.gpc.flush_type.raw_scrub, core)
core.uop_ld_st_prediction.pki = rollup.pki(core.lsu.uop_ld_st_prediction, core)
core.sf_predictor_accuracy = (
    core.lsu.uop_ld_st_prediction - core.lsu.uop_ld_wait_error - core.lsu.uop_ld_raw_scrub_detected
) / core.lsu.uop_ld_st_prediction

# PMU events
# Note regarding cleaving: "slots" refers to "dispatch slots after all possible cleaving occurs", ie it remains 4 per cycle.
core.pmu.idr_stall_ixu_sched = rollup.sum_by_regex(core.idr, r"stall_backend_sch_[A-T]\d$")
core.pmu.idr_stall_fsu_sched = rollup.sum_by_regex(core.idr, r"stall_backend_(fsu_[u-z]|sch_[U-Z][0-9])$")
core.pmu.stall_slot = metric.cycles * machine_width - (core.pmu.op_spec - core.pmu.op_cleaved_spec)
core.pmu.stall_slot_backend = machine_width * core.idr.backend_stall
core.pmu.stall_slot_frontend = core.pmu.stall_slot - core.pmu.stall_slot_backend
core.pmu.stall = core.idr.backend_stall + core.idr.frontend_stall

# Stalls
core.stalls = core.pmu.stall
core.stalls_pki = rollup.pki(core.pmu.stall, core)
core.stalls_frontend_pki = rollup.pki(core.pmu.stall_frontend, core)
core.stalls_backend_pki = rollup.pki(core.pmu.stall_backend, core)
core.stall_rate = rollup.per_cycle(core.pmu.stall, core)
core.stall_frontend_rate = rollup.per_cycle(core.pmu.stall_frontend, core)
core.stall_backend_rate = rollup.per_cycle(core.pmu.stall_backend, core)
core.stall_backend_mem_rate = rollup.per_cycle(core.pmu.stall_backend_mem, core)

# Top Down Accounting metrics
# FIXME: needs to be adjustable for number of schedulers/pipes
tda = metric.tda
ixu_width = 6 if core.ixu_width.empty else core.ixu_width
fsu_width = 3 if core.fsu_width.empty else core.fsu_width * core.idr.fsu_partitions

tda.slots = core.pmu.cpu_cycles * machine_width
tda.l1.retired = core.pmu.op_retired / tda.slots
tda.l1.lost = ((core.pmu.op_spec - core.pmu.op_cleaved_spec) - core.pmu.op_retired) / tda.slots
tda.l1.frontend = core.pmu.stall_slot_frontend / tda.slots
tda.l1.backend = core.pmu.stall_slot_backend / tda.slots

tda.l2.frontend_latency = (core.pmu.stall_frontend * machine_width) / tda.slots
tda.l2.frontend_bandwidth = (core.pmu.stall_slot_frontend - (core.pmu.stall_frontend * machine_width)) / tda.slots
tda.l2.backend_memory = rollup.per_cycle(core.pmu.stall_backend_membound, core)
tda.l2.backend_core = (core.pmu.stall_slot_backend - (core.pmu.stall_backend_membound * machine_width)) / tda.slots
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
tda.l3.frontend_bandwidth_isolate = (core.pmu.op_isolated_spec * 3) / tda.slots
if (tda.l3.frontend_bandwidth_isolate - tda.frontend_bandwidth).empty:
    tda.l3.frontend_bandwith_isolate = tda.frontend_bandwidth
if not core.pmu.stall_frontend_iq_bw.empty or not core.pmu.stall_frontend_decoder.empty:
    tda.l3.frontend_bandwidth_iq_bw = (
        core.pmu.stall_frontend_iq_bw / (core.pmu.stall_frontend_iq_bw + core.pmu.stall_frontend_decoder)
    ) * (tda.l2.frontend_bandwidth - tda.l3.frontend_bandwidth_isolate)
    tda.l3.frontend_bandwidth_decoder = (
        core.pmu.stall_frontend_decoder / (core.pmu.stall_frontend_iq_bw + core.pmu.stall_frontend_decoder)
    ) * (tda.l2.frontend_bandwidth - tda.l3.frontend_bandwidth_isolate)
else:
    tda.l3.frontend_bandwidth_decoder = 0
    tda.l3.frontend_bandwidth_iq_bw = 0
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
tda.l3.backend_core_serialize = (
    core.pmu.idr_stall_drain / (core.pmu.stall_backend_resource + core.pmu.idr_stall_drain)
) * tda.l2.backend_core
tda.l3.backend_core_schedulers = (
    (core.pmu.idr_stall_ixu_sched + core.pmu.idr_stall_fsu_sched)
    / (core.pmu.stall_backend_resource + core.pmu.idr_stall_drain)
) * tda.l2.backend_core
tda.l3.backend_core_other = (
    (core.pmu.stall_backend_resource - (core.pmu.idr_stall_ixu_sched + core.pmu.idr_stall_fsu_sched))
    / (core.pmu.stall_backend_resource + core.pmu.idr_stall_drain)
) * tda.l2.backend_core
tda.l3.retired_pipe_utilization_ixu = core.pmu.ixu_num_uops_issued / (core.pmu.cpu_cycles * ixu_width)
tda.l3.retired_pipe_utilization_fsu = core.pmu.fsu_issued / (core.pmu.cpu_cycles * fsu_width)

tda.l3.lost_branch_mispredict_ind = core.pmu.br_ind_mis_pred_retired / core.pmu.br_mis_pred_retired
tda.l3.lost_branch_mispredict_ret = core.pmu.br_return_mis_pred_retired / core.pmu.br_mis_pred_retired
tda.l3.lost_branch_mispredict_imm = core.pmu.br_immed_mis_pred_retired / core.pmu.br_mis_pred_retired
tda.l3.lost_branch_mispredict_taken = core.pmu.br_taken_mis_pred_retired / core.pmu.br_mis_pred_retired
tda.l3.lost_branch_mispredict_skip = core.pmu.br_skip_mis_pred_retired / core.pmu.br_mis_pred_retired

tda.l4.backend_core_resource_rob = rollup.per_cycle(core.pmu.idr_stall_rob_id, core)
tda.l4.backend_core_resource_lob = rollup.per_cycle(core.pmu.idr_stall_lob_id, core)
tda.l4.backend_core_resource_sob = rollup.per_cycle(core.pmu.idr_stall_sob_id, core)
tda.l4.backend_core_resource_ixu = rollup.per_cycle(core.pmu.idr_stall_ixu_sched, core)
tda.l4.backend_core_resource_fsu = rollup.per_cycle(core.pmu.idr_stall_fsu_sched, core)
tda.l4.backend_core_resource_bob = rollup.per_cycle(core.pmu.idr_stall_bob_id, core)
tda.l4.backend_core_resource_rename = rollup.per_cycle(core.pmu.stall_backend_rename, core)
tda.l4.backend_core_exec_units = rollup.per_cycle(core.pmu.stall_backend_busy, core)
tda.l4.backend_core_dependancies = rollup.per_cycle(core.pmu.stall_backend_ilock, core)
tda.l4.frontend_btc_clear = rollup.per_cycle(core.pmu.stall_frontend_btc_clear, core)
tda.l4.frontend_btb_clear = rollup.per_cycle(core.pmu.stall_frontend_btb_clear, core)
tda.l4.stall_backend_ld_on_st = rollup.per_cycle(core.pmu.stall_backend_ld_wait_on_st, core)

tda.l4.backend_core_busy_p0a = rollup.per_cycle(core.pmu.stall_backend_busy_p0_a, core)
tda.l4.backend_core_busy_p0b = rollup.per_cycle(core.pmu.stall_backend_busy_p0_b, core)
tda.l4.backend_core_busy_p0j = rollup.per_cycle(core.pmu.stall_backend_busy_p0_j, core)
tda.l4.backend_core_busy_p0k = rollup.per_cycle(core.pmu.stall_backend_busy_p0_k, core)
tda.l4.backend_core_busy_p0m = rollup.per_cycle(core.pmu.stall_backend_busy_p0_m, core)
tda.l4.backend_core_busy_p0n = rollup.per_cycle(core.pmu.stall_backend_busy_p0_n, core)
tda.l4.backend_core_busy_p1a = rollup.per_cycle(core.pmu.stall_backend_busy_p1_a, core)
tda.l4.backend_core_busy_p1b = rollup.per_cycle(core.pmu.stall_backend_busy_p1_b, core)
tda.l4.backend_core_busy_p1j = rollup.per_cycle(core.pmu.stall_backend_busy_p1_j, core)
tda.l4.backend_core_busy_p1k = rollup.per_cycle(core.pmu.stall_backend_busy_p1_k, core)
tda.l4.backend_core_busy_p1m = rollup.per_cycle(core.pmu.stall_backend_busy_p1_m, core)
tda.l4.backend_core_busy_p1n = rollup.per_cycle(core.pmu.stall_backend_busy_p1_n, core)
tda.l4.backend_core_busy_p0x = rollup.per_cycle(core.pmu.stall_backend_busy_p0_x, core)
tda.l4.backend_core_busy_p0y = rollup.per_cycle(core.pmu.stall_backend_busy_p0_y, core)
tda.l4.backend_core_busy_p1x = rollup.per_cycle(core.pmu.stall_backend_busy_p1_x, core)
tda.l4.backend_core_busy_p1y = rollup.per_cycle(core.pmu.stall_backend_busy_p1_y, core)

tda.l4.backend_core_ilock_p0a = rollup.per_cycle(core.pmu.stall_backend_ilock_p0_a, core)
tda.l4.backend_core_ilock_p0b = rollup.per_cycle(core.pmu.stall_backend_ilock_p0_b, core)
tda.l4.backend_core_ilock_p0j = rollup.per_cycle(core.pmu.stall_backend_ilock_p0_j, core)
tda.l4.backend_core_ilock_p0k = rollup.per_cycle(core.pmu.stall_backend_ilock_p0_k, core)
tda.l4.backend_core_ilock_p0m = rollup.per_cycle(core.pmu.stall_backend_ilock_p0_m, core)
tda.l4.backend_core_ilock_p0n = rollup.per_cycle(core.pmu.stall_backend_ilock_p0_n, core)
tda.l4.backend_core_ilock_p1a = rollup.per_cycle(core.pmu.stall_backend_ilock_p1_a, core)
tda.l4.backend_core_ilock_p1b = rollup.per_cycle(core.pmu.stall_backend_ilock_p1_b, core)
tda.l4.backend_core_ilock_p1j = rollup.per_cycle(core.pmu.stall_backend_ilock_p1_j, core)
tda.l4.backend_core_ilock_p1k = rollup.per_cycle(core.pmu.stall_backend_ilock_p1_k, core)
tda.l4.backend_core_ilock_p1m = rollup.per_cycle(core.pmu.stall_backend_ilock_p1_m, core)
tda.l4.backend_core_ilock_p1n = rollup.per_cycle(core.pmu.stall_backend_ilock_p1_n, core)
tda.l4.backend_core_ilock_p0x = rollup.per_cycle(core.pmu.stall_backend_ilock_p0_x, core)
tda.l4.backend_core_ilock_p0y = rollup.per_cycle(core.pmu.stall_backend_ilock_p0_y, core)
tda.l4.backend_core_ilock_p1x = rollup.per_cycle(core.pmu.stall_backend_ilock_p1_x, core)
tda.l4.backend_core_ilock_p1y = rollup.per_cycle(core.pmu.stall_backend_ilock_p1_y, core)

tda.l4.retired_pipe_utilization_ixu_p0a = rollup.per_cycle(core.pmu.ixu_ixa_par0_issued, core)
tda.l4.retired_pipe_utilization_ixu_p0b = rollup.per_cycle(core.pmu.ixu_ixb_par0_issued, core)
tda.l4.retired_pipe_utilization_ixu_p0j = rollup.per_cycle(core.pmu.ixu_ixj_par0_issued, core)
tda.l4.retired_pipe_utilization_ixu_p0k = rollup.per_cycle(core.pmu.ixu_ixk_par0_issued, core)
tda.l4.retired_pipe_utilization_ixu_p0m = (
    core.pmu.ixu_ixc_par0_issued + core.pmu.ixu_ixd_par0_issued
) / core.pmu.cpu_cycles
tda.l4.retired_pipe_utilization_ixu_p0n = (
    core.pmu.ixu_ixe_par0_issued + core.pmu.ixu_ixf_par0_issued
) / core.pmu.cpu_cycles
tda.l4.retired_pipe_utilization_ixu_p1a = rollup.per_cycle(core.pmu.ixu_ixa_par1_issued, core)
tda.l4.retired_pipe_utilization_ixu_p1b = rollup.per_cycle(core.pmu.ixu_ixb_par1_issued, core)
tda.l4.retired_pipe_utilization_ixu_p1j = rollup.per_cycle(core.pmu.ixu_ixj_par1_issued, core)
tda.l4.retired_pipe_utilization_ixu_p1k = rollup.per_cycle(core.pmu.ixu_ixk_par1_issued, core)
tda.l4.retired_pipe_utilization_ixu_p1m = (
    core.pmu.ixu_ixc_par1_issued + core.pmu.ixu_ixd_par1_issued
) / core.pmu.cpu_cycles
tda.l4.retired_pipe_utilization_ixu_p1n = (
    core.pmu.ixu_ixe_par1_issued + core.pmu.ixu_ixf_par1_issued
) / core.pmu.cpu_cycles
tda.l4.retired_pipe_utilization_fsu_p0x = rollup.per_cycle(core.pmu.fsu_fsx_par0_issued, core)
tda.l4.retired_pipe_utilization_fsu_p0y = rollup.per_cycle(core.pmu.fsu_fsy_par0_issued, core)
tda.l4.retired_pipe_utilization_fsu_p1x = rollup.per_cycle(core.pmu.fsu_fsx_par1_issued, core)
tda.l4.retired_pipe_utilization_fsu_p1y = rollup.per_cycle(core.pmu.fsu_fsy_par1_issued, core)

tda.l4.stall_backend_ofb_full = rollup.per_cycle(core.pmu.stall_backend_ofb_full, core)
tda.l4.l2c_fb_full_sched_stall = rollup.per_cycle(core.pmu.l2c_fbfull_schstall, core)
tda.l4.l2c_fb_full_bank_stall = rollup.per_cycle(core.pmu.l2c_fbfull_bnkstall, core)

tda.l4.backend_core_resource_int_rf = rollup.per_cycle(core.pmu.stall_backend_rename_int, core)
tda.l4.backend_core_resource_fp_rf = rollup.per_cycle(core.pmu.stall_backend_rename_fp, core)
tda.l4.backend_core_resource_flag_rf = rollup.per_cycle(core.pmu.stall_backend_rename_flag, core)

# Non-PMU
tda.l4.backend_core_resource_int_rf = rollup.per_cycle(core.idr.stall_backend_int_rf, core)
tda.l4.backend_core_resource_fp_rf = rollup.per_cycle(core.idr.stall_backend_fp_rf, core)
tda.l4.backend_core_resource_flag_rf = rollup.per_cycle(core.idr.stall_backend_flag_rf, core)

# Compute the BW in GB/s from the core to the mesh
core.bus_bw_rd = rollup.cache.bandwidth(core.pmu.bus_access_rd, metric)
core.bus_bw_wr = rollup.cache.bandwidth(core.pmu.bus_access_wr, metric)
core.bus_bw = rollup.cache.bandwidth(core.pmu.bus_access, metric)

# Compute cache fill/miss BWs in GB/s
core.dcache.fill_bw = rollup.cache.bandwidth(core.pmu.l1d_cache_refill, metric)
core.icache.fill_bw = rollup.cache.bandwidth(core.pmu.l1i_cache_refill, metric)
core.l2cache.fill_bw = rollup.cache.bandwidth(core.pmu.l2d_cache_refill, metric)

core.dcache.read_miss_bw = rollup.cache.bandwidth(core.pmu.l1d_cache_lmiss_rd, metric)
core.icache.miss_bw = rollup.cache.bandwidth(core.pmu.l1i_cache_lmiss, metric)
core.l2cache.read_miss_bw = rollup.cache.bandwidth(core.pmu.l2d_cache_lmiss_rd, metric)

# PEM per instruction, for power folks
pem = metric.pem
pem.l1d_cache.pkc = rollup.pkc(core.pmu.l1d_cache, core)
pem.l1d_cache_refill.pkc = rollup.pkc(core.pmu.l1d_cache_refill, core)
pem.l2d_cache.pkc = rollup.pkc(core.pmu.l2d_cache, core)
pem.l2d_cache_refill.pkc = rollup.pkc(core.pmu.l2d_cache_refill, core)
pem.ld_spec.pkc = rollup.pkc(core.pmu.ld_spec, core)
pem.st_spec.pkc = rollup.pkc(core.pmu.st_spec, core)
pem.dp_spec.pkc = rollup.pkc(core.pmu.dp_spec, core)
pem.ase_spec.pkc = rollup.pkc(core.pmu.ase_spec, core)
pem.vfp_spec.pkc = rollup.pkc(core.pmu.vfp_spec, core)
pem.pc_write_spec.pkc = rollup.pkc(core.pmu.pc_write_spec, core)
pem.l1i_cache.pkc = rollup.pkc(core.pmu.l1i_cache, core)
pem.gpc_flush.pkc = rollup.pkc(core.pmu.gpc_flush, core)
pem.bus_access_rd.pkc = rollup.pkc(core.pmu.bus_access_rd, core)
pem.bus_access_wr.pkc = rollup.pkc(core.pmu.bus_access_wr, core)
