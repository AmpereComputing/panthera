//
// Copyright (c) 2019-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <fmt/format.h>
#include "ampere/archx/instruction.h"
#include "ampere/archx/decoder_input.h"

extern int decode_address_translation(const unsigned char* bytes);
extern int decode_cache_maint(const unsigned char* bytes);
extern int decode_exception(const unsigned char* bytes);
extern int decode_divide(const unsigned char* bytes);

namespace ampere::archx {

std::vector<DecoderFunctions> Instruction::decoders;

const InstInfo& Instruction::inst_info() const
{
    static InstInfo no_inst_info{};

    if (!m_inst_info) {
        if (decoders.size() == 0)
            throw std::runtime_error{"Attempted to decode instruction information without any decoders set"};
        for (auto& dec : decoders) {
            m_inst_info = dec.decode_inst_info(opcode());

            if (m_inst_info)
                return *m_inst_info;
        }

        m_inst_info = &no_inst_info;
    }

    return *m_inst_info;
}

const InstInfoFusion& Instruction::inst_info_fusion(std::uint32_t* opcodes)
{
    if (decoders.size() == 0)
        throw std::runtime_error{"Attempted to decode fusion instruction information without any decoders set"};
    for (auto& dec : decoders) {
        auto info = dec.decode_inst_info_fusion(opcodes);
        if (info)
            return *info;
    }
    static InstInfoFusion no_inst_info{};
    return no_inst_info;
}

AddressTranslationType Instruction::address_translation_type() const
{
    DecoderInput di(opcode());
    return AddressTranslationType(decode_address_translation(di.ptr_no_mode()));
}

adis::sys_alias_t Instruction::cache_maint_type() const
{
    if (is_cache_maint()) {
        return sys_qualifier();
    }
    return adis::sys_alias_t::INVALID;
}

ExceptionType Instruction::exception_type() const
{
    DecoderInput di(opcode());
    return ExceptionType(decode_exception(di.ptr_no_mode()));
}

DivideType Instruction::divide_type() const
{
    DecoderInput di(opcode());
    return DivideType(decode_divide(di.ptr_no_mode()));
}

adis::encoding_enum_t Instruction::prefetch_type() const
{
    const auto encoding = adis_info();
    if (adis::prefetch(encoding)) {
        return encoding;
    }
    return adis::ENC_INVALID;
}

SysReg Instruction::msr_sysreg() const
{
    uint32_t encoding = opcode();
    uint8_t op0 = (encoding >> 19) & 0b1;
    uint8_t op1 = (encoding >> 16) & 0b111;
    uint8_t crn = (encoding >> 12) & 0b1111;
    uint8_t crm = (encoding >> 8) & 0b1111;
    uint8_t op2 = (encoding >> 5) & 0b111;

    return SysReg(op0, op1, crn, crm, op2);
}

int Instruction::divide_srcs() const
{
    switch (inst_info().div_srcs) {
    case DivSource::ADVSIMD_SQRT:
    case DivSource::SVE_SQRT:
        return 1;
    case DivSource::ADVSIMD_FDIV:
    case DivSource::GPR_IDIV:
    case DivSource::SVE_FDIV:
    case DivSource::SVE_IDIV:
        return 2;
    default:
        return 0;
    }
}

LReg Instruction::divide_src(int idx) const
{
    DivSource div_srcs = inst_info().div_srcs;

    switch (div_srcs) {
    case DivSource::GPR_IDIV:
        if (idx == 0)
            return cvt_gpr((opcode() & 0b1111100000) >> 5);
        else if (idx == 1)
            return cvt_gpr((opcode() & 0b111110000000000000000) >> 16);
        break;
    case DivSource::ADVSIMD_SQRT:
        if (idx == 0)
            return cvt_vec((opcode() & 0b1111100000) >> 5);
        break;
    case DivSource::ADVSIMD_FDIV:
        if (idx == 0)
            return cvt_vec((opcode() & 0b1111100000) >> 5);
        else if (idx == 1)
            return cvt_vec((opcode() & 0b111110000000000000000) >> 16);
        break;
    case DivSource::SVE_FDIV:
    case DivSource::SVE_IDIV:
        if (idx == 0)
            return cvt_vec(opcode() & 0x1F);
        else if (idx == 1)
            return cvt_vec((opcode() >> 5) & 0x1F);
        break;
    case DivSource::SVE_SQRT:
        if (idx == 0)
            return cvt_vec((opcode() >> 5) & 0x1F);
        break;
    case DivSource::INVALID:
        break;
    }

    return LReg::INVALID;
}

LReg Instruction::tlbi_src() const
{
    return cvt_gpr(opcode() & 0b11111);
}

uint16_t Instruction::tlbi_by_va_asid() const
{
    AMPERE_ASSERT(is_tlbi_by_va(), "Tried to get TLBI source data but instruction is not a TLBI by VA");
    auto reg_src = state().read(tlbi_src());

    // ASID is Xt[63:48]
    return (reg_src >> 48) & 0xffff;
}

uint8_t Instruction::tlbi_by_va_ttl() const
{
    AMPERE_ASSERT(is_tlbi_by_va(), "Tried to get TLBI source data but instruction is not a TLBI by VA");
    auto reg_src = state().read(tlbi_src());

    // TTL is Xt[47:44];
    return (reg_src >> 44) & 0xf;
}

VirtAddr Instruction::tlbi_by_va_addr() const
{
    AMPERE_ASSERT(is_tlbi_by_va(), "Tried to get TLBI source data but instruction is not a TLBI by VA");
    auto reg_src = state().read(tlbi_src());

    // address[55:12] is Xt[43:0]
    int64_t va = (reg_src & 0xfffffffffff) << 12;

    // Sign extend the address
    va = (va << 8) >> 8;

    return VirtAddr(va);
}

UopSet Instruction::decode(std::uint32_t opcode, DecoderMode decoder_mode)
{
    UopSet uops;

    for (auto& dec : decoders) {
        uops = dec.decode(opcode, decoder_mode);

        if (!uops.empty())
            return uops;
    }
    if (decoders.size() == 0)
        throw std::runtime_error{"Attempted to decode instruction to uops without any decoders set"};

    return {};
}

UopSet Instruction::decode(DecoderMode decoder_mode)
{
    return decode(opcode(), decoder_mode);
}

const char* Instruction::decode_source(std::uint32_t opcode, DecoderMode decoder_mode)
{
    const char* source{nullptr};
    for (auto& dec : decoders) {
        source = dec.decode_source(opcode, decoder_mode);

        if (source != nullptr)
            return source;
    }
    if (decoders.size() == 0)
        throw std::runtime_error{"Attempted to decode instruction to uops without any decoders set"};

    return "Unknown";
}

const char* Instruction::decode_source(DecoderMode decoder_mode)
{
    return decode_source(opcode(), decoder_mode);
}

std::pair<archx::FusionType, archx::UopSet> Instruction::decode_fusion(std::uint32_t* opcodes,
                                                                       bool skip_checks,
                                                                       DecoderMode decoder_mode)
{
    std::pair<archx::FusionType, archx::UopSet> uops;

    for (auto& dec : decoders) {
        auto uops = dec.decode_fusion(opcodes, skip_checks, decoder_mode);

        if (!uops.second.empty())
            return uops;
    }

    if (decoders.size() == 0)
        throw std::runtime_error{"Attempted to decode instruction to uops without any decoders set"};

    return {};
}

void Instruction::set_decoders(const std::vector<std::string>& decoder_names)
{
    decoders.clear();

    for (auto& name : decoder_names) {
        try {
            auto& functions = get_decoders().at(name);
            decoders.push_back(functions);
        } catch (std::out_of_range&) {
            throw std::runtime_error{fmt::format("no registered decoder named {}", name)};
        }
    }
}

} // namespace ampere::archx
