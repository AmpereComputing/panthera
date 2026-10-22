//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include "ampere/mesh/chi/types.h"
#include "ampere/mesh/mesh_module.h"
#include "ampere/sc/sc.h"

namespace ampere::mesh {

using namespace sc;
using namespace archx;

using util::factor2;
using util::mask;

enum SNRMode {
    SNR1 = 1,
    SNR2 = 2,
    SNR4 = 4,
    SNR8 = 8
};

struct SAMRegion
{
    enum class Type {
        DDR,
        CXL,
        COM,
        MMC
    };

    Type type;
    PhysAddr base;
    size_t size;
    SNRMode snr_mode;
    int snr_index; // 0..7
    int banger_bits{0};
};

struct SystemAddressMapIfc : public sc_core::sc_interface
{
    virtual const SAMRegion& lookup(PhysAddr addr) const = 0;
};

class SystemAddressMap : public Module, public SystemAddressMapIfc
{
public:
    explicit SystemAddressMap(const sc_core::sc_module_name& name) : Module{name} {}

    const SAMRegion& lookup(PhysAddr addr) const override;

    void end_of_elaboration() override;

private:
    using SAMRegions = std::vector<SAMRegion>;

    Param<SNRMode> ddr_snr_mode{"ddr_snr_mode", "SNR mode for DDR memory", SNR1};
    Param<SNRMode> cxl_snr_mode{"cxl_snr_mode", "SNR mode for CXL memory", SNR1};
    Param<int> ddr_banger_bits{"ddr_banger_bits",
                               "\"Banger\" for DDR (can be: 0, 1, 2; 1 means send 2 sequential cache lines to the same "
                               "target, 2 means send 4 lines",
                               0};

    // By default, all of memory (48-bit physical address) is given over to DDR. This is suitable
    // enough for tests, which are the places where it's annoyting to have to keep specifying an
    // address map.
    //
    Param<json> regions_param{"regions", "Address region definitions", R"({
        "ddr_0": {
            "base": 0,
            "size": 281474976710656
        }
    })"_json};
    SAMRegions regions;

    void build_regions();
    std::pair<SAMRegion::Type, int> decode_region_name(const std::string& region_name);
};

class SAM : public MeshModule, poly<SAM>
{
public:
    explicit SAM(const sc_core::sc_module_name& name) : MeshModule{name}, die_id{get_die_id()} {}
    virtual NodeId lookup(PhysAddr addr) const
    {
        AMPERE_FATAL("lookup called in generic SAM; missing SAM type?");
        return {};
    }

    ConfigPort<SystemAddressMapIfc> system_address_map{"system_address_map"};

protected:
    static constexpr int CL_BITS = 6;
    static constexpr int CCLS = 8; // FIXME: parametrize?

    Param<bool> permute_on{"permute", "Enable/disable permutations (default: on)", true};

    uint8_t die_id;

    uint64_t permute(uint64_t a) const;
};

class RN_SAM : public SAM, poly<RN_SAM>
{
public:
    explicit RN_SAM(const sc_core::sc_module_name& name) : SAM{name} {}

    Param<std::vector<NodeId>> tgt_lut{"tgt_lut", "LUT of HNF targets", "[]"_json};

    NodeId lookup(PhysAddr addr) const override;

    void end_of_elaboration() override
    {
        auto n = tgt_lut.val().size();
        auto k = factor2(n);
        nbits = k;
        mod = n >> k;
    }

private:
    int nbits;
    int mod;
};

class CDRN_SAM : public SAM, poly<CDRN_SAM>
{
public:
    explicit CDRN_SAM(const sc_core::sc_module_name& name);

    Param<std::vector<uint8_t>> tgt_lut_param{"tgt_lut", "LUT of PxHA targets", "[]"_json};

    NodeId lookup(PhysAddr addr) const override;

private:
    std::vector<NodeId> tgt_lut;
};

class PXHA_SAM : public SAM, poly<PXHA_SAM>
{
public:
    explicit PXHA_SAM(const sc_core::sc_module_name& name) : SAM{name}
    {
        if (pmha_tgt_param->is_array())
            pmha_tgt_param->get_to(pmha_tgt);
        else if (pmha_tgt_param->is_number_integer())
            pmha_tgt.push_back(pmha_tgt_param->get<uint8_t>());
        else
            AMPERE_ERROR("bad JSON type for pmha_tgt (should be integer or array of integers)");
    }

    Param<json> pmha_tgt_param{
        "pmha_tgt", "Target PMHA agent id (either an integer, or integers indexed like the die_lut)", 0};
    Param<std::vector<uint8_t>> die_lut{
        "die_lut", "Map from die index to actual die id", "[0, 1, 2, 3, 4, 5, 6, 7]"_json};

    NodeId lookup(PhysAddr addr) const override;

private:
    std::vector<uint8_t> pmha_tgt;
};

class IDRN_SAM : public SAM, poly<IDRN_SAM>
{
public:
    explicit IDRN_SAM(const sc_core::sc_module_name& name) : SAM{name} {}
    using SAM::SAM;

    Param<std::vector<uint8_t>> tgt_lut{"tgt_lut", "LUT of PmHA targets (agent id only)", "[]"_json};
    Param<std::vector<uint8_t>> die_lut{
        "die_lut", "Map from die index to actual die id", "[0, 1, 2, 3, 4, 5, 6, 7]"_json};

    NodeId lookup(PhysAddr addr) const override;
};

class PMHA_SAM : public SAM, poly<PMHA_SAM>
{
public:
    explicit PMHA_SAM(const sc_core::sc_module_name& name) : SAM{name} {}

    Param<uint8_t> mcu_tgt{"mcu_tgt", "Target agent for DDR addresses", 0};
    Param<std::vector<NodeId>> cxl_tgt{"cxl_tgt", "Target agents for CXL addresses", "[]"_json};
    Param<NodeId> com_tgt{"com_tgt", "Target agent COM addresses", NodeId{}};

    NodeId lookup(PhysAddr addr) const override;
};

class PMHA_RSAM : public SAM, poly<PMHA_RSAM>
{
public:
    explicit PMHA_RSAM(const sc_core::sc_module_name& name);

    Param<std::vector<uint8_t>> pxha_tgt{"pxha_tgt", "Target agents for PmHA", "[]"_json};

    NodeId lookup(PhysAddr addr) const override;

private:
    std::vector<NodeId> pxha_tgt_lut;
};

class HNF_SAM : public SAM, poly<HNF_SAM>
{
public:
    explicit HNF_SAM(const sc_core::sc_module_name& name) : SAM{name} {}

    Param<std::vector<NodeId>> tgt_lut{"tgt_lut", "Target agents for DDR addresses", "[]"_json};
    Param<int> grouping{"grouping", "Grouping of cache line addresses (1, 2, 4)", 4};

    NodeId lookup(PhysAddr addr) const override;
};

class TDMA_SAM : public SAM, poly<TDMA_SAM>
{
public:
    explicit TDMA_SAM(const sc_core::sc_module_name& name);

    Param<std::vector<uint8_t>> hbm_lut_param{"hbm_lut", "Target agents for HBM addresses", "[]"_json};
    Param<std::vector<uint8_t>> mhat_lut_param{"mhat_lut", "Target agents for TMEM addresses", "[]"_json};
    Param<bool> cluster_mode{"cluster_mode", "Choose cluster mode instead of slice mode", false};

    NodeId lookup(PhysAddr addr) const override;

private:
    std::vector<NodeId> hbm_lut;
    std::vector<NodeId> mhat_lut;
};

} // namespace ampere::mesh
