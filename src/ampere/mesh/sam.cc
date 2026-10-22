//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/mesh/sam.h"
#include "ampere/util/bits.h"

namespace ampere::mesh {

void SystemAddressMap::end_of_elaboration()
{
    build_regions();
}

const SAMRegion& SystemAddressMap::lookup(PhysAddr addr) const
{
    for (const auto& r : regions) {
        if (addr >= r.base && addr < r.base + r.size)
            return r;
    }

    fmt::print("Defined memory regions:\n");
    for (const auto& r : regions)
        fmt::print("  {} - {}\n", r.base, r.base + r.size - 1);

    AMPERE_FATAL("{}: address {} does not map to a region", name(), addr);
    static SAMRegion no_region;
    return no_region;
}

void SystemAddressMap::build_regions()
{
    AMPERE_ASSERT(regions_param->is_object(), "regions is not a JSON object");

    for (const auto& [k, v] : regions_param->items()) {
        auto [type, snr_index] = decode_region_name(k);

        SAMRegion region;
        region.type = type;
        region.snr_index = snr_index;

        v["base"].get_to(region.base);
        v["size"].get_to(region.size);

        switch (region.type) {
        case SAMRegion::Type::DDR:
            region.snr_mode = ddr_snr_mode;
            region.banger_bits = ddr_banger_bits;
            break;
        case SAMRegion::Type::CXL:
            region.snr_mode = cxl_snr_mode;
            break;
        case SAMRegion::Type::COM:
        case SAMRegion::Type::MMC:
            region.snr_mode = SNR8;
        }

        regions.push_back(region);
    }

    for (auto& r : regions) {
        switch (r.type) {
        case SAMRegion::Type::DDR:
            r.snr_mode = ddr_snr_mode;
            break;
        case SAMRegion::Type::CXL:
            r.snr_mode = cxl_snr_mode;
            break;
        case SAMRegion::Type::COM:
        case SAMRegion::Type::MMC:
            r.snr_mode = SNR8;
            break;
        }
    }
}

uint64_t SAM::permute(uint64_t a) const
{
    constexpr int PERM_BITS = 10;

    constexpr auto R = [](uint32_t x) -> uint32_t {
        uint32_t reversed = 0;

        for (size_t i = 0; i < PERM_BITS; ++i)
            reversed |= ((x >> i) & 1) << ((PERM_BITS - 1) - i);

        return reversed;
    };

    // With a more modern C++ version, this could be replaced with:
    //    std::popcount(x) & 1
    //
    constexpr auto xor_bits = [](uint32_t x) -> uint32_t {
        uint32_t bit = 0;
        while (x) {
            bit ^= x & 1;
            x >>= 1;
        }
        return bit;
    };

    static std::array<uint32_t, PERM_BITS> perm = {R(0b1001001101),
                                                   R(0b1111011011),
                                                   R(0b0011000011),
                                                   R(0b0010100001),
                                                   R(0b1100111011),
                                                   R(0b0001010001),
                                                   R(0b1010010101),
                                                   R(0b0011001101),
                                                   R(0b1011100100),
                                                   R(0b0001100000)};

    uint64_t aa = a & ~mask(PERM_BITS);

    for (size_t i = 0; i < PERM_BITS; ++i)
        aa |= xor_bits(a & perm[i]) << i;

    return aa;
}

std::pair<SAMRegion::Type, int> SystemAddressMap::decode_region_name(const std::string& region_name)
{
    auto i = region_name.find('_');

    if (i == std::string::npos)
        AMPERE_ERROR("System Address Map region name should look like <type>_# (got {})", region_name);

    auto type_name = region_name.substr(0, i);
    auto snr_index = std::stoi(region_name.substr(i + 1));

    SAMRegion::Type type{SAMRegion::Type::DDR};

    if (type_name == "ddr")
        type = SAMRegion::Type::DDR;
    else if (type_name == "cxl")
        type = SAMRegion::Type::CXL;
    else if (type_name == "com")
        type = SAMRegion::Type::COM;
    else if (type_name == "mmc")
        type = SAMRegion::Type::MMC;
    else
        AMPERE_ERROR("unrecognized memory region type {} (should be ddr, cxl, com, mmc)", type_name);

    return {type, snr_index};
}

NodeId RN_SAM::lookup(PhysAddr addr) const
{
    const auto& region = system_address_map->lookup(addr);

    uint64_t d_addr = (addr - region.base) >> CL_BITS;
    uint64_t o_addr = d_addr;
    uint64_t index = 0;

    if (nbits != 0) {
        while (d_addr) {
            index ^= (d_addr & mask(nbits));
            d_addr >>= nbits;
        }
    }

    // incorporate the non-power-of-two portion of the LUT size
    index = index * mod + (o_addr % mod);

    return tgt_lut.val().at(index);
}

CDRN_SAM::CDRN_SAM(const sc_core::sc_module_name& name) : SAM{name}
{
    for (const auto agent_id : tgt_lut_param.val())
        tgt_lut.emplace_back(NodeId::DieType::CPU, 0, die_id, agent_id);
}

NodeId CDRN_SAM::lookup(PhysAddr addr) const
{
    const auto& region = system_address_map->lookup(addr);

    uint64_t d_addr = (addr - region.base) >> (CL_BITS + region.banger_bits);
    uint64_t p_addr = permute_on ? permute(d_addr) : d_addr;

    int die_bits = log2(CCLS / region.snr_mode);

    unsigned int n = tgt_lut.size();
    AMPERE_ASSERT(n > 0, "{}: tgt_lut is not defined", name());

    if (region.type == SAMRegion::Type::COM || region.type == SAMRegion::Type::MMC)
        n = 1 << factor2(n);

    int index = (p_addr >> die_bits) % n;

    return tgt_lut.at(index);
}

NodeId PXHA_SAM::lookup(PhysAddr addr) const
{
    const auto& region = system_address_map->lookup(addr);

    uint64_t d_addr = (addr - region.base) >> CL_BITS;
    uint64_t p_addr = permute_on ? permute(d_addr) : d_addr;

    int die_bits = log2(CCLS / region.snr_mode);

    uint8_t die = (p_addr & mask(die_bits)) + region.snr_index;
    uint8_t agent = pmha_tgt.size() == 1 ? pmha_tgt.at(0) : pmha_tgt.at(die);

    return NodeId{NodeId::DieType::CPU, 0 /*socket*/, die_lut.val().at(die), agent};
}

NodeId IDRN_SAM::lookup(PhysAddr addr) const
{
    const auto& region = system_address_map->lookup(addr);

    uint64_t d_addr = (addr - region.base) >> CL_BITS;
    uint64_t p_addr = permute_on ? permute(d_addr) : d_addr;

    int die_bits = log2(CCLS / region.snr_mode);

    // Extracting the grp bits
    int grp_bits = (p_addr >> die_bits) & 0x3;
    // Extracting the mod bits, placing it by left shifting by 2 grp bits and
    // add it to grp bits
    int index = (((p_addr >> (die_bits + 3)) % 3) << 2) | grp_bits;

    AMPERE_ASSERT(tgt_lut.val().size() == 12, "Tgt LUT size should be 12");
    AMPERE_ASSERT(index <= 12, " Value of this index is greater than tgt_lut size");

    uint8_t die = (p_addr & mask(die_bits)) + region.snr_index;

    return NodeId{NodeId::DieType::CPU, 0 /*socket*/, die_lut.val().at(die), tgt_lut.val().at(index)};
}

NodeId PMHA_SAM::lookup(PhysAddr addr) const
{
    const auto& region = system_address_map->lookup(addr);

    if (region.type == SAMRegion::Type::DDR)
        return NodeId{NodeId::DieType::CPU, 0, die_id, mcu_tgt};
    else if (region.type == SAMRegion::Type::CXL)
        return cxl_tgt.val().at(0); // FIXME: striping
    else if (region.type == SAMRegion::Type::COM)
        return com_tgt;
    else {
        AMPERE_FATAL("{}: unsupported region type", name());
        return NodeId{};
    }
}

PMHA_RSAM::PMHA_RSAM(const sc_core::sc_module_name& name) : SAM{name}
{
    for (const auto agent_id : pxha_tgt.val())
        pxha_tgt_lut.emplace_back(NodeId::DieType::CPU, 0, die_id, agent_id);
}

NodeId PMHA_RSAM::lookup(PhysAddr addr) const
{
    const auto& region = system_address_map->lookup(addr);

    uint64_t d_addr = (addr - region.base) >> CL_BITS;
    uint64_t p_addr = permute_on ? permute(d_addr) : d_addr;

    int die_bits = log2(CCLS / region.snr_mode);

    AMPERE_ASSERT(pxha_tgt_lut.size() > 0, "{}: pxha_tgt_lut is not defined", name());

    // TODO, the magic number 2 here needs to be changed to something generic which represents
    // min(log2(number_of_pxhas_per_ccl), log2(number_of_pmhas_per_ccl))
    int index = ((p_addr >> die_bits) >> 2) & 0x1;

    return pxha_tgt_lut.at(index);
}

NodeId HNF_SAM::lookup(PhysAddr addr) const
{
    const auto& region = system_address_map->lookup(addr);

    if (region.type == SAMRegion::Type::DDR) {
        uint64_t d_addr = (addr - region.base) >> CL_BITS;

        if (grouping == 2)
            d_addr >>= 1;
        else if (grouping == 4)
            d_addr >>= 2;

        auto index = d_addr % tgt_lut.val().size();
        return tgt_lut.val().at(index);
    } else {
        AMPERE_FATAL("{}: unsupported region type", name());
        return NodeId{};
    }
}

TDMA_SAM::TDMA_SAM(const sc_core::sc_module_name& name) : SAM{name}
{
    for (const auto agent_id : hbm_lut_param.val())
        hbm_lut.emplace_back(NodeId::DieType::CPU, 0, die_id, agent_id);

    for (const auto agent_id : mhat_lut_param.val())
        mhat_lut.emplace_back(NodeId::DieType::CPU, 0, die_id, agent_id);
}

NodeId TDMA_SAM::lookup(PhysAddr addr) const
{
    if (addr.value() < 0x1000000000) {
        auto perm = util::fold_to_n_bits((addr >> 9), 5);
        size_t index{0};

        if (cluster_mode) {
            auto cluster = util::extract(addr.value(), 39, 36);
            index = (cluster << 5) + (perm);
        } else {
            auto slice = util::extract(addr.value(), 39, 34);
            index = (slice << 3) + (perm & 0b111);
        }

        return hbm_lut.at(index);
    } else {
        auto tmem = util::extract(addr.value(), 32, 25);
        return mhat_lut.at(tmem);
    }
}

} // namespace ampere::mesh
