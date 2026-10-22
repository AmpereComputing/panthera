//
// Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/benchmarks/common/constants.h"

#define PREFETCH_DISTANCE 64
#define PREFETCH_SPACING 64
#define DEMAND_TO_PREFETCH_RATIO 16
#define DATA_SIZE BYTES_1M

.align 8

#define PREFETCH_TYPE PLDL1KEEP
.global prefetch_load_data_l1
prefetch_load_data_l1:
#include "_prefetch_data.S"
#undef PREFETCH_TYPE

#define PREFETCH_TYPE PLDL2KEEP
.global prefetch_load_data_l2
prefetch_load_data_l2:
#include "_prefetch_data.S"
#undef PREFETCH_TYPE

#define STORES

#define PREFETCH_TYPE PSTL1KEEP
.global prefetch_store_data_l1
prefetch_store_data_l1:
#include "_prefetch_data.S"
#undef PREFETCH_TYPE

#define PREFETCH_TYPE PSTL2KEEP
.global prefetch_store_data_l2
prefetch_store_data_l2:
#include "_prefetch_data.S"
#undef PREFETCH_TYPE

#define PREFETCH_TYPE PSTL1STRM
.global prefetch_streaming_store_data_l1
prefetch_streaming_store_data_l1:
#include "_prefetch_data.S"
#undef PREFETCH_TYPE

#define PREFETCH_TYPE PSTL2STRM
.global prefetch_streaming_store_data_l2
prefetch_streaming_store_data_l2:
#include "_prefetch_data.S"
#undef PREFETCH_TYPE
