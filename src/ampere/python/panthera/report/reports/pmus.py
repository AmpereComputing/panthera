# Copyright (c) 2020-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Correlation scatter plots and speedups for PMU metrics"""

import logging

import pandas as pd
from panthera.report.helpers import *

# Note: These are broken up into lists by expected correlation method so that
# they can be split apart more easily if/when the need arises to report on
# those groups independently
any_method = [
    # Connor PMUs
    "sys.soc_0.core_0.pmu.simd_inst_retired",
    "sys.soc_0.core_0.pmu.ase_inst_retired",
    "sys.soc_0.core_0.pmu.ase_sve_inst_retired",
    "sys.soc_0.core_0.pmu.simd_inst_spec",
    "sys.soc_0.core_0.pmu.ase_inst_spec",
    "sys.soc_0.core_0.pmu.ase_sve_inst_spec",
    "sys.soc_0.core_0.pmu.ase_uop_spec",
    "sys.soc_0.core_0.pmu.ase_sve_uop_spec",
    "sys.soc_0.core_0.pmu.ase_fp_spec",
    "sys.soc_0.core_0.pmu.ase_sve_fp_spec",
    "sys.soc_0.core_0.pmu.ase_sve_fp_div_spec",
    "sys.soc_0.core_0.pmu.ase_sve_fp_sqrt_spec",
    "sys.soc_0.core_0.pmu.ase_sve_fp_fma_spec",
    "sys.soc_0.core_0.pmu.ase_sve_fp_mul_spec",
    "sys.soc_0.core_0.pmu.ase_int_spec",
    "sys.soc_0.core_0.pmu.ase_nonfp_spec",
    "sys.soc_0.core_0.pmu.ase_sve_nonfp_spec",
    "sys.soc_0.core_0.pmu.ase_sve_ldst_spec",
    "sys.soc_0.core_0.pmu.prf_spec",
    "sys.soc_0.core_0.pmu.fp_scale_ops_spec",
    "sys.soc_0.core_0.pmu.int_scale_ops_spec",
    "sys.soc_0.core_0.pmu.stall_backend_atomic",
    "sys.soc_0.core_0.pmu.br_uncond_retired",
    "sys.soc_0.core_0.pmu.br_cond_retired",
    "sys.soc_0.core_0.pmu.br_cond_taken_retired",
    "sys.soc_0.core_0.pmu.ase_sve_fp_fma_mul_spec",
    "sys.soc_0.core_0.pmu.ase_sve_fp_dot_mmla_spec",
    "sys.soc_0.core_0.pmu.ase_sve_int_mul_mmla_dot_spec",
    "sys.soc_0.core_0.pmu.op_cleaved_spec",
    # Move the l2c ones to "improved_l2_access"
    # "sys.soc_0.core_0.pmu.l2c_cache_hit",
    # "sys.soc_0.core_0.pmu.l2c_cache_hit_demand",
    # "sys.soc_0.core_0.pmu.l2c_cache_fhit_hwprf",
    # "sys.soc_0.core_0.pmu.l2c_prefetch_lfb_hit",
    # "sys.soc_0.core_0.pmu.l2c_miss_percyc",
    # "sys.soc_0.core_0.pmu.l2c_cache_inst",
    "sys.soc_0.core_0.pmu.bpu_ind_no_predict",
    "sys.soc_0.core_0.pmu.bpu_ind_no_predict_retired",
    # "sys.soc_0.core_0.pmu.bpu_ind_no_predict_stall",       # moved to Unit DV
    "sys.soc_0.core_0.pmu.iq_valid_percyc",
    "sys.soc_0.core_0.pmu.lsu_sfp_mispred_fwd",
    "sys.soc_0.core_0.pmu.lsu_sfp_mispred_skip",
    "sys.soc_0.core_0.pmu.lsu_sfp_aux_hit",
    "sys.soc_0.core_0.pmu.lsu_sfp_aux_alloc",
    "sys.soc_0.core_0.pmu.lsu_sfp_aux_mispred",
    "sys.soc_0.core_0.pmu.lsu_sfp_aux_evict",
    "sys.soc_0.core_0.pmu.inst_fetch_percyc",
    "sys.soc_0.core_0.pmu.int_fixed_ops_spec",
    "sys.soc_0.core_0.pmu.fp_fixed_ops_spec",
    "sys.soc_0.core_0.pmu.sve_fp_spec",
    "sys.soc_0.core_0.pmu.sve_inst_retired",
    "sys.soc_0.core_0.pmu.sve_inst_spec",
    "sys.soc_0.core_0.pmu.sve_int_spec",
    "sys.soc_0.core_0.pmu.sve_ld_gather_spec",
    "sys.soc_0.core_0.pmu.sve_uop_spec",
    "sys.soc_0.core_0.pmu.sve_perm_spec",
    "sys.soc_0.core_0.pmu.sve_movprfx_spec",
    "sys.soc_0.core_0.pmu.sve_ldst_spec",
    # "sys.soc_0.core_0.pmu.sve_ldst_preg_spec",     # moved to Unit DV
    "sys.soc_0.core_0.pmu.sve_ldst_zreg_spec",
    "sys.soc_0.core_0.pmu.sve_ldst_contig_spec",
    "sys.soc_0.core_0.pmu.sve_ldst_multi_spec",
    "sys.soc_0.core_0.pmu.sve_ldst_noncontig_spec",
    "sys.soc_0.core_0.pmu.sve_st_scatter_spec",
    # Trinity PMUs
    # replaced with L2C_CACHE_INST
    # "sys.soc_0.core_0.pmu.l2i_cache",
    # change in behavior
    "sys.soc_0.core_0.pmu.l2d_cache",
    # not implemented in RTL
    # "sys.soc_0.core_0.pmu.l2i_cache_lmiss",
    # change in behavior
    "sys.soc_0.core_0.pmu.l2d_cache_lmiss_rd",
    "sys.soc_0.core_0.pmu.fp_spec",
    "sys.soc_0.core_0.pmu.int_spec",
    "sys.soc_0.core_0.pmu.inst_fetch",
    "sys.soc_0.core_0.pmu.l1i_cache_rd",
    "sys.soc_0.core_0.pmu.bpu_rsb_full_stall",
    "sys.soc_0.core_0.pmu.bpu_hit_indirect_predictor_ittage",
    "sys.soc_0.core_0.pmu.bpu_btb_clear_conditional_mispredict",
    "sys.soc_0.core_0.pmu.bpu_btb_clear_unconditional_mispredict",
    "sys.soc_0.core_0.pmu.bpu_btb_clear_ind_target_mispredict",
    "sys.soc_0.core_0.pmu.bpu_btc_clear_bad_prediction",
    "sys.soc_0.core_0.pmu.bpu_btc_clear_btb_miss",
    "sys.soc_0.core_0.pmu.cmo_spec",
    # "sys.soc_0.core_0.pmu.tlbi_spec",           # moved to Unit DV
    "sys.soc_0.core_0.pmu.bpu_conditional_branch_miss_btb",
    "sys.soc_0.core_0.pmu.bpu_btb_clear",
    "sys.soc_0.core_0.pmu.bpu_btc_clear",
    # "sys.soc_0.core_0.pmu.idr_throttle_cycles",        # moved to Unit DV
    # "sys.soc_0.core_0.pmu.idr_throttle_uops_spec",     # moved to Unit DV
    "sys.soc_0.core_0.pmu.stall_backend_busy",
    "sys.soc_0.core_0.pmu.stall_backend_ilock",
    "sys.soc_0.core_0.pmu.stall_backend_membound",
    "sys.soc_0.core_0.pmu.stall_backend_cpubound",
    "sys.soc_0.core_0.pmu.stall_backend_rename",
    # Legacy PMUs
    # uncorrelatable - no hits
    # "sys.soc_0.core_0.pmu.sw_incr",
    "sys.soc_0.core_0.pmu.l1i_cache_refill",
    "sys.soc_0.core_0.pmu.l1d_cache_refill",
    "sys.soc_0.core_0.pmu.l1d_cache",
    "sys.soc_0.core_0.pmu.l1d_tlb_refill",
    "sys.soc_0.core_0.pmu.ld_retired",
    "sys.soc_0.core_0.pmu.st_retired",
    "sys.soc_0.core_0.pmu.inst_retired",
    # "sys.soc_0.core_0.pmu.exc_taken",       # moved to Unit DV
    # "sys.soc_0.core_0.pmu.exc_return",      # moved to Unit DV
    "sys.soc_0.core_0.pmu.id_write_retired",
    "sys.soc_0.core_0.pmu.pc_write_retired",
    "sys.soc_0.core_0.pmu.br_immed_retired",
    "sys.soc_0.core_0.pmu.br_return_retired",
    # "sys.soc_0.core_0.pmu.unaligned_ldst_retired",     # moved to Unit DV
    "sys.soc_0.core_0.pmu.br_mis_pred",
    "sys.soc_0.core_0.pmu.cpu_cycles",
    "sys.soc_0.core_0.pmu.br_pred",
    "sys.soc_0.core_0.pmu.mem_access",
    "sys.soc_0.core_0.pmu.l2d_cache_refill",
    "sys.soc_0.core_0.pmu.l2d_cache_wb",
    "sys.soc_0.core_0.pmu.bus_access",
    "sys.soc_0.core_0.pmu.inst_spec",
    # "sys.soc_0.core_0.pmu.ttbr_write_retired",     # moved to Unit DV
    # uncorrelatable - no hits
    # "sys.soc_0.core_0.pmu.chain",
    "sys.soc_0.core_0.pmu.br_retired",
    "sys.soc_0.core_0.pmu.br_mis_pred_retired",
    "sys.soc_0.core_0.pmu.stall_backend",
    "sys.soc_0.core_0.pmu.l1d_tlb",
    "sys.soc_0.core_0.pmu.l1d_cache_lmiss_rd",
    "sys.soc_0.core_0.pmu.op_retired",
    "sys.soc_0.core_0.pmu.op_spec",
    "sys.soc_0.core_0.pmu.l1d_cache_rd",
    "sys.soc_0.core_0.pmu.l1d_cache_wr",
    "sys.soc_0.core_0.pmu.l1d_cache_refill_rd",
    "sys.soc_0.core_0.pmu.l1d_tlb_refill_rd",
    "sys.soc_0.core_0.pmu.l1d_tlb_refill_wr",
    "sys.soc_0.core_0.pmu.l1d_tlb_rd",
    "sys.soc_0.core_0.pmu.l1d_tlb_wr",
    "sys.soc_0.core_0.pmu.l2d_cache_rd",
    "sys.soc_0.core_0.pmu.l2d_cache_wr",
    "sys.soc_0.core_0.pmu.l2d_cache_refill_rd",
    "sys.soc_0.core_0.pmu.l2d_cache_refill_wr",
    "sys.soc_0.core_0.pmu.l2d_cache_wb_victim",
    "sys.soc_0.core_0.pmu.l2d_cache_wb_clean",
    "sys.soc_0.core_0.pmu.l2d_tlb_refill_rd",
    "sys.soc_0.core_0.pmu.l2d_tlb_refill_wr",
    "sys.soc_0.core_0.pmu.l2d_tlb_rd",
    "sys.soc_0.core_0.pmu.l2d_tlb_wr",
    "sys.soc_0.core_0.pmu.bus_access_rd",
    "sys.soc_0.core_0.pmu.bus_access_wr",
    "sys.soc_0.core_0.pmu.mem_access_rd",
    "sys.soc_0.core_0.pmu.mem_access_wr",
    "sys.soc_0.core_0.pmu.ldrex_spec",
    "sys.soc_0.core_0.pmu.strex_spec",
    "sys.soc_0.core_0.pmu.ld_spec",
    "sys.soc_0.core_0.pmu.st_spec",
    "sys.soc_0.core_0.pmu.ldst_spec",
    "sys.soc_0.core_0.pmu.dp_spec",
    "sys.soc_0.core_0.pmu.ase_spec",
    "sys.soc_0.core_0.pmu.vfp_spec",
    "sys.soc_0.core_0.pmu.pc_write_spec",
    "sys.soc_0.core_0.pmu.crypto_spec",
    "sys.soc_0.core_0.pmu.br_immed_spec",
    "sys.soc_0.core_0.pmu.br_return_spec",
    "sys.soc_0.core_0.pmu.br_indirect_spec",
    "sys.soc_0.core_0.pmu.isb_spec",
    "sys.soc_0.core_0.pmu.dsb_spec",
    "sys.soc_0.core_0.pmu.dmb_spec",
    # "sys.soc_0.core_0.pmu.csdb_spec",      # moved to Unit DV
    # "sys.soc_0.core_0.pmu.exc_svc",        # moved to Unit DV
    # "sys.soc_0.core_0.pmu.exc_pabort",     # moved to Unit DV
    # "sys.soc_0.core_0.pmu.exc_dabort",     # moved to Unit DV
    "sys.soc_0.core_0.pmu.rc_ld_spec",
    "sys.soc_0.core_0.pmu.rc_st_spec",
    "sys.soc_0.core_0.pmu.l2_prefetch_refill",
    "sys.soc_0.core_0.pmu.l2_prefetch_upgrade",
    "sys.soc_0.core_0.pmu.bpu_conditional_branch_hit_btb",
    "sys.soc_0.core_0.pmu.bpu_btb_update",
    "sys.soc_0.core_0.pmu.icf_inst_spec_decode",
    "sys.soc_0.core_0.pmu.gpc_flush_mem_fault",
    # uncorrelatable - no hits
    # "sys.soc_0.core_0.pmu.l2c_dat_crd_stall",
    # "sys.soc_0.core_0.pmu.l2c_rsp_crd_stall",
    # "sys.soc_0.core_0.pmu.l2c_req_crd_stall",
    "sys.soc_0.core_0.pmu.stall_backend_mem",
    "sys.soc_0.core_0.pmu.l1i_cache_lmiss",
    "sys.soc_0.core_0.pmu.ldst_align_lat",
    "sys.soc_0.core_0.pmu.ld_align_lat",
    "sys.soc_0.core_0.pmu.st_align_lat",
    # "sys.soc_0.core_0.pmu.mem_access_checked",         # moved to Unit DV
    # "sys.soc_0.core_0.pmu.mem_access_checked_rd",      # moved to Unit DV
    # "sys.soc_0.core_0.pmu.mem_access_checked_wr",      # moved to Unit DV
    "sys.soc_0.core_0.pmu.br_skip_retired",
    "sys.soc_0.core_0.pmu.br_immed_taken_retired",
    "sys.soc_0.core_0.pmu.br_indnr_taken_retired",
    "sys.soc_0.core_0.pmu.br_immed_pred_retired",
    "sys.soc_0.core_0.pmu.br_immed_mis_pred_retired",
    "sys.soc_0.core_0.pmu.br_ind_pred_retired",
    "sys.soc_0.core_0.pmu.br_ind_mis_pred_retired",
    "sys.soc_0.core_0.pmu.br_return_pred_retired",
    "sys.soc_0.core_0.pmu.br_return_mis_pred_retired",
    "sys.soc_0.core_0.pmu.br_indnr_pred_retired",
    "sys.soc_0.core_0.pmu.br_indnr_mis_pred_retired",
    "sys.soc_0.core_0.pmu.br_taken_pred_retired",
    "sys.soc_0.core_0.pmu.br_taken_mis_pred_retired",
    "sys.soc_0.core_0.pmu.br_skip_pred_retired",
    "sys.soc_0.core_0.pmu.br_skip_mis_pred_retired",
    "sys.soc_0.core_0.pmu.br_pred_retired",
    "sys.soc_0.core_0.pmu.br_ind_retired",
    "sys.soc_0.core_0.pmu.l1d_cache_rw",
    "sys.soc_0.core_0.pmu.l1d_cache_prfm",
    "sys.soc_0.core_0.pmu.l1d_cache_refill_prfm",
    "sys.soc_0.core_0.pmu.stall_backend_l1d",
    "sys.soc_0.core_0.pmu.stall_backend_tlb",
    "sys.soc_0.core_0.pmu.icf_early_mis_pred",
    "sys.soc_0.core_0.pmu.icf_feq_full",
    "sys.soc_0.core_0.pmu.icf_inst_fifo_full",
    "sys.soc_0.core_0.pmu.icf_stall",
    "sys.soc_0.core_0.pmu.icf_pc_fifo_full",
    "sys.soc_0.core_0.pmu.idr_stall_bob_id",
    "sys.soc_0.core_0.pmu.idr_stall_lob_id",
    "sys.soc_0.core_0.pmu.idr_stall_sob_id",
    "sys.soc_0.core_0.pmu.idr_stall_rob_id",
    # replaced with STALL_FRONTEND_FLUSH
    # "sys.soc_0.core_0.pmu.idr_stall_flush",
    # "sys.soc_0.core_0.pmu.idr_stall_swob_timeout",     # moved to Unit DV
    # "sys.soc_0.core_0.pmu.idr_stall_swob_raw",         # moved to Unit DV
    # "sys.soc_0.core_0.pmu.idr_stall_swob_full",        # moved to Unit DV
    # replaced with STALL_BACKEND_L1D
    # "sys.soc_0.core_0.pmu.stall_backend_cache",
    "sys.soc_0.core_0.pmu.ixu_issue_cancel",
    "sys.soc_0.core_0.pmu.ixu_load_cancel",
    "sys.soc_0.core_0.pmu.ixu_slow_cancel",
    "sys.soc_0.core_0.pmu.fsu_cancel",
    # "sys.soc_0.core_0.pmu.gpc_swob_drain",         # moved to Unit DV
    "sys.soc_0.core_0.pmu.l1d_tlb_miss",
    "sys.soc_0.core_0.pmu.ofb_full",
    "sys.soc_0.core_0.pmu.ld_from_st_fwd",
    "sys.soc_0.core_0.pmu.l1_prefetch_ld_fill",
    "sys.soc_0.core_0.pmu.l1_prefetch_l2_req",
    "sys.soc_0.core_0.pmu.l1_prefetch_dist_rst",
    "sys.soc_0.core_0.pmu.l1_prefetch_entry_trained",
    "sys.soc_0.core_0.pmu.lsu_st_retire_stall",
    "sys.soc_0.core_0.pmu.l1d_cache_refill_rw",
    # "sys.soc_0.core_0.pmu.l2c_pipe_replay",        # moved to Unit DV
    "sys.soc_0.core_0.pmu.l2c_inst_refill",
    "sys.soc_0.core_0.pmu.l2c_data_refill",
    "sys.soc_0.core_0.pmu.l2_prefetch_req",
    "sys.soc_0.core_0.pmu.gpc_flush",
    "sys.soc_0.core_0.pmu.l1i_tlb_miss",
    "sys.soc_0.core_0.pmu.l1i_tlb",
    "sys.soc_0.core_0.pmu.l1i_tlb_refill",
    "sys.soc_0.core_0.pmu.l1i_cache",
    "sys.soc_0.core_0.pmu.l1_prefetch_dist_inc",
    "sys.soc_0.core_0.pmu.stall_frontend",
    "sys.soc_0.core_0.pmu.stall_backend_resource",
    "sys.soc_0.core_0.pmu.l2d_tlb_refill",
    "sys.soc_0.core_0.pmu.l2i_tlb_refill",
    "sys.soc_0.core_0.pmu.mmu_d_otb_alloc",
    "sys.soc_0.core_0.pmu.mmu_i_otb_alloc",
    "sys.soc_0.core_0.pmu.mmu_d_s1_walk_cache_lookup",
    "sys.soc_0.core_0.pmu.mmu_i_s1_walk_cache_lookup",
    "sys.soc_0.core_0.pmu.mmu_d_s1_walk_cache_refill",
    "sys.soc_0.core_0.pmu.mmu_i_s1_walk_cache_refill",
    "sys.soc_0.core_0.pmu.dtlb_walk",
    "sys.soc_0.core_0.pmu.itlb_walk",
    "sys.soc_0.core_0.pmu.mmu_d_trans_cache_hit_s1l2_walk",
    "sys.soc_0.core_0.pmu.mmu_d_trans_cache_hit_s1l1_walk",
    "sys.soc_0.core_0.pmu.mmu_d_trans_cache_hit_s1l0_walk",
    "sys.soc_0.core_0.pmu.mmu_i_trans_cache_hit_s1l2_walk",
    "sys.soc_0.core_0.pmu.mmu_i_trans_cache_hit_s1l1_walk",
    "sys.soc_0.core_0.pmu.mmu_i_trans_cache_hit_s1l0_walk",
    "sys.soc_0.core_0.pmu.mmu_d_s1_walk_fault",
    "sys.soc_0.core_0.pmu.mmu_i_s1_walk_fault",
    "sys.soc_0.core_0.pmu.mmu_d_walk_steps",
    "sys.soc_0.core_0.pmu.mmu_i_walk_steps",
    "sys.soc_0.core_0.pmu.ixu_ixa_par0_issued",
    "sys.soc_0.core_0.pmu.ixu_ixb_par0_issued",
    "sys.soc_0.core_0.pmu.ixu_ixc_par0_issued",
    "sys.soc_0.core_0.pmu.ixu_ixd_par0_issued",
    "sys.soc_0.core_0.pmu.ixu_ixa_par1_issued",
    "sys.soc_0.core_0.pmu.ixu_ixb_par1_issued",
    "sys.soc_0.core_0.pmu.ixu_ixc_par1_issued",
    "sys.soc_0.core_0.pmu.fsu_fsx_issued",
    "sys.soc_0.core_0.pmu.fsu_fsy_issued",
    "sys.soc_0.core_0.pmu.fsu_fsz_issued",
    "sys.soc_0.core_0.pmu.bpu_hit_btb",
    "sys.soc_0.core_0.pmu.bpu_hit_indirect_predictor",
    "sys.soc_0.core_0.pmu.bpu_hit_rsb",
    "sys.soc_0.core_0.pmu.bpu_unconditional_branch_miss_btb",
    "sys.soc_0.core_0.pmu.bpu_branch_no_hit",
    "sys.soc_0.core_0.pmu.bpu_hit_btb_and_mispredict",
    "sys.soc_0.core_0.pmu.bpu_conditional_branch_hit_btb_and_mispredict",
    "sys.soc_0.core_0.pmu.bpu_indirect_branch_hit_btb_and_mispredict",
    "sys.soc_0.core_0.pmu.bpu_hit_rsb_and_mispredict",
    "sys.soc_0.core_0.pmu.bpu_miss_rsb_and_mispredict",
    "sys.soc_0.core_0.pmu.bpu_no_prediction_mispredict",
    "sys.soc_0.core_0.pmu.ixu_num_uops_issued",
    "sys.soc_0.core_0.pmu.ixu_ixa_issued",
    "sys.soc_0.core_0.pmu.ixu_ixb_issued",
    "sys.soc_0.core_0.pmu.ixu_ixc_issued",
    "sys.soc_0.core_0.pmu.ixu_ixd_issued",
    "sys.soc_0.core_0.pmu.fsu_issued",
    "sys.soc_0.core_0.pmu.ixu_ixe_issued",
    "sys.soc_0.core_0.pmu.idr_stall_ixu_sched",
    "sys.soc_0.core_0.pmu.idr_stall_fsu_sched",
    "sys.soc_0.core_0.pmu.stall_slot",
    "sys.soc_0.core_0.pmu.stall_slot_backend",
    "sys.soc_0.core_0.pmu.stall",
]
improved_speculation = [
    "sys.soc_0.core_0.pmu.l1d_cache_miss",
    "sys.soc_0.core_0.pmu.l1d_cache_hwprf",
    "sys.soc_0.core_0.pmu.l1d_cache_refill_hwprf",
    "sys.soc_0.core_0.pmu.l1_prefetch_gr_adt_trigger",
    "sys.soc_0.core_0.pmu.l1_prefetch_gr_rt_insert",
    "sys.soc_0.core_0.pmu.l1_prefetch_gr_producer_fill",
    "sys.soc_0.core_0.pmu.l1d_cache_hwprf_gr",
    "sys.soc_0.core_0.pmu.l1d_cache_refill_hwprf_gr",
    "sys.soc_0.core_0.pmu.l1_prefetch_gr_l2_req",
    "sys.soc_0.core_0.pmu.loop_mode_enter",
    "sys.soc_0.core_0.pmu.loop_mode_looped",
    "sys.soc_0.core_0.pmu.loop_mode_inst_fetched",
    "sys.soc_0.core_0.pmu.loop_mode_cycle_cntr",
    "sys.soc_0.core_0.pmu.idr_stall_drain",
    "sys.soc_0.core_0.pmu.dtlb_walk_percyc",
    "sys.soc_0.core_0.pmu.itlb_walk_percyc",
    "sys.soc_0.core_0.pmu.l1d_tlb_rw",
    "sys.soc_0.core_0.pmu.l1i_tlb_rd",
    "sys.soc_0.core_0.pmu.l1d_tlb_prfm",
    "sys.soc_0.core_0.pmu.dtlb_step",
    "sys.soc_0.core_0.pmu.itlb_step",
    "sys.soc_0.core_0.pmu.dtlb_walk_large",
    # "sys.soc_0.core_0.pmu.itlb_walk_large",        # moved to Unit DV
    "sys.soc_0.core_0.pmu.dtlb_walk_small",
    "sys.soc_0.core_0.pmu.itlb_walk_small",
    "sys.soc_0.core_0.pmu.l2i_tlb",
    "sys.soc_0.core_0.pmu.l2d_tlb",
    # "sys.soc_0.core_0.pmu.l1d_cache_refill_wr",     # moved to Unit DV
    "sys.soc_0.core_0.pmu.bpu_bhr_full_stall",
    "sys.soc_0.core_0.pmu.sve_pred_spec",
    "sys.soc_0.core_0.pmu.sve_pred_empty_spec",
    "sys.soc_0.core_0.pmu.sve_pred_full_spec",
    "sys.soc_0.core_0.pmu.sve_pred_partial_spec",
]
improved_stall_backend = []
perfect_fe = []
improved_l2_access = [
    "sys.soc_0.core_0.pmu.l2c_cache_hit",
    "sys.soc_0.core_0.pmu.l2c_cache_hit_demand",
    "sys.soc_0.core_0.pmu.l2c_cache_fhit_hwprf",
    "sys.soc_0.core_0.pmu.l2c_prefetch_lfb_hit",
    "sys.soc_0.core_0.pmu.l2c_miss_percyc",
    "sys.soc_0.core_0.pmu.l2c_cache_inst",
]
improved_stall_frontend = [
    "sys.soc_0.core_0.pmu.stall_frontend_l1i",
    "sys.soc_0.core_0.pmu.stall_frontend_mem",
    "sys.soc_0.core_0.pmu.stall_frontend_cpubound",
    "sys.soc_0.core_0.pmu.stall_frontend_membound",
    "sys.soc_0.core_0.pmu.stall_frontend_flush",
    "sys.soc_0.core_0.pmu.stall_frontend_flow",
    "sys.soc_0.core_0.pmu.stall_frontend_tlb",
    "sys.soc_0.core_0.pmu.stall_frontend_cache",
    "sys.soc_0.core_0.pmu.stall_slot_frontend",
]
tbd = []

sup_cycles = speedup(df, "sys.soc_0.core_0.pmu.cpu_cycles")
sup_cycles_20pct = sup_cycles.loc[(sup_cycles.iloc[:, 1] >= 0.90) & (sup_cycles.iloc[:, 1] <= 1.10)]
df_20pct = df.loc[df[TRACE].isin(sup_cycles_20pct.index)]

spec_low = df_20pct.loc[df_20pct["tda.l1.lost"] <= 0.10]

# improved_stall_backend
sb_sup_cycles = speedup(df_20pct, "sys.soc_0.core_0.pmu.stall_backend")
sb_sup_cycles_20pct = sb_sup_cycles.loc[(sb_sup_cycles.iloc[:, 1] >= 0.90) & (sb_sup_cycles.iloc[:, 1] <= 1.10)]
sb_df_20pct = df.loc[df[TRACE].isin(sb_sup_cycles_20pct.index)]

# improved_stall_frontend
sf_sup_cycles = speedup(df_20pct, "sys.soc_0.core_0.pmu.stall_frontend")
sf_sup_cycles_20pct = sf_sup_cycles.loc[(sf_sup_cycles.iloc[:, 1] >= 0.90) & (sf_sup_cycles.iloc[:, 1] <= 1.10)]
sf_df_20pct = df.loc[df[TRACE].isin(sf_sup_cycles_20pct.index)]

# improved_l2_access
l2access_sup_cycles = speedup(df_20pct, "sys.soc_0.core_0.pmu.l2d_cache")
l2access_sup_cycles_20pct = l2access_sup_cycles.loc[
    (l2access_sup_cycles.iloc[:, 1] >= 0.90) & (l2access_sup_cycles.iloc[:, 1] <= 1.10)
]
l2access_df_20pct = df.loc[df[TRACE].isin(l2access_sup_cycles_20pct.index)]


def metric_correlation_reports(data, metrics):
    aggregates = []
    for metric in metrics:
        try:
            aggregates.append(correlation_report(data, metric=metric))
        except IndexError:
            logging.warning(f"{metric} not present in one or more configurations, skipping")
    return aggregates


means = []
means.extend(metric_correlation_reports(df_20pct, any_method))
means.extend(metric_correlation_reports(spec_low, improved_speculation))
means.extend(metric_correlation_reports(l2access_df_20pct, improved_l2_access))
means.extend(metric_correlation_reports(sb_df_20pct, improved_stall_backend))
means.extend(metric_correlation_reports(sf_df_20pct, improved_stall_frontend))

summary = pd.DataFrame(means)
summary["rtl_panthera_ratio"] = summary.iloc[:, 1].div(summary.iloc[:, 0])
print(summary)
create_csv(summary, "pmu_correlation_summary.csv")
