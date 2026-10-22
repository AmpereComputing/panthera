//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <sstream>
#include "adis-sysreg-map.h"
namespace adis {

static std::string sysreg_name(std::map<std::uint32_t,const std::string>& smap,
                               std::uint32_t s)
{
    const auto& r = smap.find(s);
    if (r != smap.end()) {
        return r->second;
    }

    sysreg_t sr(s);
    std::ostringstream os;
    os << 's'
       << sr.op0() << '_'
       << sr.op1() << '_'
       << sr.CRn() << '_'
       << sr.CRm() << '_'
       << sr.op2();
    return os.str();
}

std::string sysreg_name_MRS(std::uint32_t s) {
    return sysreg_name(sysreg_map_MRS, s);
}
std::string sysreg_name_MSR_reg(std::uint32_t s) {
    return sysreg_name(sysreg_map_MSR_reg, s);
}
std::string sysreg_name_MSR_imm(std::uint32_t s) {
    return sysreg_name(sysreg_map_MSR_imm, s);
}
std::string sysreg_name_MRRS(std::uint32_t s) {
    return sysreg_name(sysreg_map_MRRS, s);
}
std::string sysreg_name_MSRR(std::uint32_t s) {
    return sysreg_name(sysreg_map_MSRR, s);
}

} // namespace
