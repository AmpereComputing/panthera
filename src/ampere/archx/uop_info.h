//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include "ampere/util/enum.h"

make_ns_enum(ampere::archx, Uopunit,
    None,
    Integer,
    Branch,
    Load,
    Store,
    FpSIMD,
    IDR
);

make_ns_enum_without_class(ampere::archx::Pipe, Uoppipe,
    NONE,
    A  = 0b1,            // IXU pipe A
    B  = 0b10,           // IXU pipe B
    BS = 0b100,          // IXU pipe B (unpartitioned slow int)
    J  = 0b1000,         // IXU pipe J
    K  = 0b10000,        // IXU pipe K
    S  = 0b100000,       // IXU pipe S (partitioned slow int)
    C  = 0b1000000,      // IXU pipe C (loads)
    D  = 0b10000000,     // IXU pipe D (stores)
    E  = 0b100000000,    // IXU pipe E (loads)
    F  = 0b1000000000,   // IXU pipe F (stores)
    X  = 0b10000000000,  // FSU pipe X
    Y  = 0b100000000000, // FSU pipe Y
    Z  = 0b1000000000000, // FSU pipe Z
    U  = 0b10000000000000,  // FSU pipe U (X By)
    V  = 0b100000000000000, // FSU pipe V (Y By, single source)
    W  = 0b1000000000000000 // FSU pipe W (Y By)
);

make_ns_enum(ampere::archx, FsuUnit,
    NONE,
    MA,  // FP Double/Single precision (FMA) unit
    HP,  // FP Half-precision (FMA) unit
    BY,  // Bypass unit
    SI,  // Scalar integer unit, includes shift (Su), add (Au), and misc unit (Xu)
    MU,  // Multiplication (integer) unit
    BU,  // BFloat16 unit
    CU,  // Crypto unit
    DV   // Divide/SQRT (FP and integer) unit
);

make_ns_enum(ampere::archx, FsuSrcType,
    NONE,
    NORMAL,
    NO_LOCAL_BYP,
    LATE
);

make_ns_enum(ampere::archx, StoreType,
    NONE,
    SUS,
    STA,
    STD,
    STDHI,
    STDV,
    STDVHI,
    SPECIAL,
    STZ,
    PREFETCH
);

make_ns_enum(ampere::archx, BarrierType,
    NONE,
    DMB_ALL,
    DMB_ST,
    DSB
);

make_ns_enum(ampere::archx, SWPrefetchType,
    NONE,
    LOAD,
    STORE,
    INSTRUCTION
);

make_ns_enum(ampere::archx, SWPrefetchLevel,
    NONE,
    L1,
    L2,
    L3
);

namespace ampere::archx {

using Uoppipe = Pipe::Uoppipe;

enum class DrainType {
    NONE,
    POST_DRAIN,
    POST_STALL
};

enum class ResourceType {
    NONE = 0,
    INT_DIVIDER = 1,
    FP_DIVIDER = 2,
    RESOURCE_COUNT = 3
};

enum class MultipleMemopType {
    NONE,
    LD2,
    LD3,
    LD4,
    ST2,
    ST3,
    ST4
};

struct UopInfo
{ // struct layout must match init code in uop_info.py
    Uopunit unit;
    FsuUnit fsu_unit;

    // fsu_unit_producing from Producing_Uop field in yaml. It is
    // used for uops that begin execution on fsu_unit but end on a
    // different unit specified here.
    FsuUnit fsu_unit_producing;
    unsigned fsu_unit_producing_suffix;
    FsuUnit fsu_crypto_unit;

    FsuSrcType fsu_src_type[4];
    bool store_with_dest;
    bool dest_no_unr_byp;
    unsigned allowed_pipes;
    unsigned latency;
    unsigned early_exit_latency;
    unsigned mem_datasize;
    StoreType store_type;
    BarrierType barrier_type;
    DrainType drain_type;
    SWPrefetchType prefetch;
    SWPrefetchLevel prefetch_level;
    bool nontemporal;
    unsigned offset_lsl;
    ResourceType resource_type;
    unsigned resource_rate;
    unsigned early_exit_resource_rate;
    unsigned resource_count;
    unsigned vector_element_count;
    unsigned vector_element_size;
    unsigned uop_op_count;
    bool pair;
    bool atomic;
    bool exclusive;
    bool acquire;
    bool release;
    bool is_i2f;
    bool is_f2i;
    bool is_mfspr;
    bool is_mtspr;
    bool is_fdiv;
    bool is_fsqrt;
    bool is_idiv;
    bool is_pac_auth;
    bool is_fsu_uvw;
    bool is_fsu_v;

    bool is_predicated;
    bool is_predicated_sampled;
    bool is_fpu_gpr_x_to_z; // FPU uops that produce GPRs after some data manipulation
    bool is_stdv_x_to_z;    // stdv-combo that first execute on pipe x and then z
    bool is_gather2;        // is the ld uop part of a gather that requires 2 memops
    bool is_gather4;        // is the ld uop part of a gather that requires 4 memops
    bool is_scatter2;       // is the st uop part of a scatter that requires 2 memops
    bool is_scatter4;       // is the st uop part of a scatter that requires 4 memops
    bool is_double_pump;    // fires from scheduler twice, back-to-back cycles
    bool nep_dest_as_src;   // when NEP=1, dest is used as src2 operand. when NEP=0, we ignore src2
    MultipleMemopType multiple_memop;
    unsigned sequence_number;  // for gather, scatter, {ld,st}{2,3,4}
    unsigned sve_merge_source; // for movprfx fusion
};

} // namespace ampere::archx
