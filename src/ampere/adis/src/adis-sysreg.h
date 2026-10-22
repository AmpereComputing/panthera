//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once
#include <cstdint>
#include <string>
namespace adis {
struct sysreg_t {
    union {
        struct {
            std::uint32_t op0 : 2; 
            std::uint32_t op1 : 3;
            std::uint32_t CRn : 4;
            std::uint32_t CRm : 4;
            std::uint32_t op2 : 3;
        } s;
        std::uint32_t u32;
    } u;

    sysreg_t(std::uint32_t s) {
        u.u32 = s;
    }

    sysreg_t(std::uint8_t op0, 
             std::uint8_t op1,
             std::uint8_t CRn,
             std::uint8_t CRm,
             std::uint8_t op2)  {
        u.s.op0 = op0;
        u.s.op1 = op1;
        u.s.CRn = CRn;
        u.s.CRm = CRm;
        u.s.op2 = op2;
    }

    std::uint32_t as_int() const {
        return u.u32;
    }
    std::uint32_t op0() const {
        return u.s.op0;
    }
    std::uint32_t op1() const {
        return u.s.op1;
    }
    std::uint32_t op2() const {
        return u.s.op2;
    }
    std::uint32_t CRn() const {
        return u.s.CRn;
    }
    std::uint32_t CRm() const {
        return u.s.CRm;
    }

};


std::string sysreg_name_MRS(std::uint32_t s);
std::string sysreg_name_MSR_reg(std::uint32_t s);
std::string sysreg_name_MSR_imm(std::uint32_t s);
std::string sysreg_name_MSRR(std::uint32_t s);
std::string sysreg_name_MRRS(std::uint32_t s);

} // namespace
