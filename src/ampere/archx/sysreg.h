//
// Copyright (c) 2019, 2021-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#ifndef AMPERE_ARCHX_SYSREG_H
#define AMPERE_ARCHX_SYSREG_H

#include <stdint.h>
#include <string_view>

#define SYS_INST 0xd5000000
#define SYS_INST_WRITE SYS_INST
#define SYS_INST_READ (SYS_INST | 0x200000)
#define SYS_INST_MASK 0xffe00000

#define SYSINST_REG_MASK (SYS_INST_MASK | 0x1fffe0)
#define SYSINST_REG_MASK_NO_CRM (SYS_INST_MASK | 0x1ff0e0)
#define SYSREG_ENCODING(sysreg) (((uint64_t)sysreg.addr()) << 5)
#define SYSREG_ENCODING_NO_CRM(sysreg) (SYSREG_ENCODING(sysreg) & ~uint64_t(0xf00))
#define IS_SYSREG_WRITE(opcode, sysreg) ((opcode & SYSINST_REG_MASK) == (SYS_INST_WRITE | SYSREG_ENCODING(sysreg)))
#define IS_SYSREG_IMM_WRITE(opcode, sysreg) \
    ((opcode & SYSINST_REG_MASK_NO_CRM) == (SYS_INST_WRITE | SYSREG_ENCODING_NO_CRM(sysreg)))
#define IS_SYS_OP(opcode, sysreg) \
    IS_SYSREG_WRITE(opcode, sysreg) // alias for SYS operations, which use the 'write' encoding
#define IS_SYSREG_READ(opcode, sysreg) ((opcode & SYSINST_REG_MASK) == (SYS_INST_READ | SYSREG_ENCODING(sysreg)))

// Specialized detection of MSR/MRS instructions, which *must* always have bit
// 20 set beyond encoding requirements for other accesses to the SYS encoding
// space. Note that this effectively means that op0 must be of the form 0b1x,
// but we opt to keep these macros distinct from other encoding space so that
// we can use them to detect MSR/MRS instructions independently. However, this
// equivalence means we can safely use the IS_SYSREG_READ/_WRITE macros to
// detect whether an identified MSR/MRS instruction is a particular sysreg.
// Additionally, MSR immediates have the low 5 bits set.
#define MSR (SYS_INST_WRITE | 0x100000)
#define MRS (SYS_INST_READ | 0x100000)
#define MSR_IMM (SYS_INST_WRITE | 0x1f)
#define MSR_MRS_MASK (SYS_INST_MASK | 0x100000)
#define MRS_IMM_MASK (MSR_MRS_MASK | 0x1f)
#define IS_MSR(opcode) ((opcode & MSR_MRS_MASK) == MSR)
#define IS_MRS(opcode) ((opcode & MSR_MRS_MASK) == MRS)
#define IS_MRS_IMM(opcode) ((opcode & MRS_IMM_MASK) == MSR_IMM)
#define IS_MSR_REG(opcode, sysreg) (IS_MSR(opcode) && IS_SYSREG_WRITE(opcode, sysreg))
#define IS_MRS_REG(opcode, sysreg) (IS_MRS(opcode) && IS_SYSREG_READ(opcode, sysreg))
#define IS_MSR_IMM_REG(opcode, sysreg) (IS_MRS_IMM(opcode) && IS_SYSREG_IMM_WRITE(opcode, sysreg))

namespace ampere {
namespace archx {

struct SysReg
{
    constexpr SysReg(std::string_view name, uint8_t op0, uint8_t op1, uint8_t crn, uint8_t crm, uint8_t op2)
        : name{name}, op0{op0}, op1{op1}, crn{crn}, crm{crm}, op2{op2}
    {
    }

    constexpr SysReg(uint8_t op0, uint8_t op1, uint8_t crn, uint8_t crm, uint8_t op2)
        : name{"UNKNOWN"}, op0{op0}, op1{op1}, crn{crn}, crm{crm}, op2{op2}
    {
    }

    constexpr bool operator<(const SysReg& other) const
    {
        if (op0 != other.op0)
            return op0 < other.op0;
        if (op1 != other.op1)
            return op1 < other.op1;
        if (crn != other.crn)
            return crn < other.crn;
        if (crm != other.crm)
            return crm < other.crm;
        return op2 < other.op2;
    }
    constexpr bool operator==(const SysReg& other) const
    {
        return (op0 == other.op0 && op1 == other.op1 && crn == other.crn && crm == other.crm && op2 == other.op2);
    }

    constexpr uint16_t addr() const
    {
        return (uint16_t)op0 << 14 | (uint16_t)op1 << 11 | (uint16_t)crn << 7 | (uint16_t)crm << 3 | (uint16_t)op2;
    }

    std::string_view name;
    const uint8_t op0, op1, crn, crm, op2;
};

struct SysInst : public SysReg
{
    constexpr SysInst(std::string_view name, uint8_t op1, uint8_t crn, uint8_t crm, uint8_t op2)
        : SysReg(name, 0b01, op1, crn, crm, op2)
    {
    }
};

// Useful system registers accessible via MRS/MSR
//                              name                op0   op1    CRn     CRm     op2
constexpr SysReg CONTEXTIDR_EL1("CONTEXTIDR_EL1", 0b11, 0b000, 0b1101, 0b0000, 0b001);
constexpr SysReg CONTEXTIDR_EL2("CONTEXTIDR_EL2", 0b11, 0b100, 0b1101, 0b0000, 0b001);
constexpr SysReg VBAR_EL1("VBAR_EL1", 0b11, 0b000, 0b1100, 0b0000, 0b000);
constexpr SysReg VBAR_EL2("VBAR_EL2", 0b11, 0b100, 0b1100, 0b0000, 0b000);
constexpr SysReg VBAR_EL3("VBAR_EL3", 0b11, 0b110, 0b1100, 0b0000, 0b000);
constexpr SysReg SCTLR_EL1("SCTLR_EL1", 0b11, 0b000, 0b0001, 0b0000, 0b000);
constexpr SysReg SCTLR_EL2("SCTLR_EL2", 0b11, 0b100, 0b0001, 0b0000, 0b000);
constexpr SysReg SCTLR_EL3("SCTLR_EL3", 0b11, 0b110, 0b0001, 0b0000, 0b000);
constexpr SysReg HCR_EL2("HCR_EL2", 0b11, 0b100, 0b0001, 0b0001, 0b000);
constexpr SysReg SCR_EL3("SCR_EL3", 0b11, 0b110, 0b0001, 0b0001, 0b000);
constexpr SysReg TCR_EL1("TCR_EL1", 0b11, 0b000, 0b0010, 0b0000, 0b010);
constexpr SysReg TCR_EL2("TCR_EL2", 0b11, 0b100, 0b0010, 0b0000, 0b010);
constexpr SysReg TCR_EL3("TCR_EL3", 0b11, 0b110, 0b0010, 0b0000, 0b010);
constexpr SysReg VTCR_EL2("VTCR_EL2", 0b11, 0b100, 0b0010, 0b0001, 0b010);
constexpr SysReg TTBR0_EL1("TTBR0_EL1", 0b11, 0b000, 0b0010, 0b0000, 0b000);
constexpr SysReg TTBR0_EL2("TTBR0_EL2", 0b11, 0b100, 0b0010, 0b0000, 0b000);
constexpr SysReg TTBR0_EL3("TTBR0_EL3", 0b11, 0b110, 0b0010, 0b0000, 0b000);
constexpr SysReg TTBR1_EL1("TTBR1_EL1", 0b11, 0b000, 0b0010, 0b0000, 0b001);
constexpr SysReg TTBR1_EL2("TTBR1_EL2", 0b11, 0b100, 0b0010, 0b0000, 0b001);
constexpr SysReg VTTBR_EL2("VTTBR_EL2", 0b11, 0b100, 0b0010, 0b0001, 0b000);
constexpr SysReg ELR_EL1("ELR_EL1", 0b11, 0b000, 0b0100, 0b0000, 0b001);
constexpr SysReg ELR_EL2("ELR_EL2", 0b11, 0b100, 0b0100, 0b0000, 0b001);
constexpr SysReg ELR_EL3("ELR_EL3", 0b11, 0b110, 0b0100, 0b0000, 0b001);
constexpr SysReg SP_EL0("SP_EL0", 0b11, 0b000, 0b0100, 0b0001, 0b000);
constexpr SysReg SP_EL1("SP_EL1", 0b11, 0b100, 0b0100, 0b0001, 0b000);
constexpr SysReg SP_EL2("SP_EL2", 0b11, 0b110, 0b0100, 0b0001, 0b000);
constexpr SysReg SPSR_EL1("SPSR_EL1", 0b11, 0b000, 0b0100, 0b0000, 0b000);
constexpr SysReg SPSR_EL2("SPSR_EL2", 0b11, 0b100, 0b0100, 0b0000, 0b000);
constexpr SysReg SPSR_EL3("SPSR_EL3", 0b11, 0b110, 0b0100, 0b0000, 0b000);
constexpr SysReg SPSel("SPSel", 0b11, 0b000, 0b0100, 0b0010, 0b000);
constexpr SysReg CurrentEL("CurrentEL", 0b11, 0b000, 0b0100, 0b0010, 0b010);
constexpr SysReg DAIF("DAIF", 0b11, 0b011, 0b0100, 0b0010, 0b001);
constexpr SysReg DAIFSet("DAIFSet", 0b00, 0b011, 0b0100, 0b0000, 0b110); // note: CRm can take any value
constexpr SysReg DAIFClr("DAIFClr", 0b00, 0b011, 0b0100, 0b0000, 0b111); // note: CRm can take any value
constexpr SysReg DIT("DIT", 0b11, 0b011, 0b0100, 0b0010, 0b101);
constexpr SysReg PAN("PAN", 0b11, 0b000, 0b0100, 0b0010, 0b011);
constexpr SysReg SSBS("SSBS", 0b11, 0b011, 0b0100, 0b0010, 0b110);
constexpr SysReg UAO("UAO", 0b11, 0b000, 0b0100, 0b0010, 0b100);
constexpr SysReg TCO("TCO", 0b11, 0b011, 0b0100, 0b0010, 0b111);
constexpr SysReg NZCV("NZCV", 0b11, 0b011, 0b0100, 0b0010, 0b000);
constexpr SysReg CPUECTLR_EL1("CPUECTLR_EL1", 0b11, 0b001, 0b1111, 0b0010, 0b001);
constexpr SysReg MAIR_EL1("MAIR_EL1", 0b11, 0b000, 0b1010, 0b0010, 0b000);
constexpr SysReg TPIDRRO_EL0("TPIDRRO_EL0", 0b11, 0b011, 0b1101, 0b0000, 0b011);
constexpr SysReg TPIDR_EL0("TPIDR_EL0", 0b11, 0b011, 0b1101, 0b0000, 0b010);
constexpr SysReg TPIDR_EL1("TPIDR_EL1", 0b11, 0b000, 0b1101, 0b0000, 0b100);
constexpr SysReg TPIDR_EL2("TPIDR_EL2", 0b11, 0b100, 0b1101, 0b0000, 0b010);
constexpr SysReg TPIDR_EL3("TPIDR_EL3", 0b11, 0b110, 0b1101, 0b0000, 0b010);
constexpr SysReg CPACR_EL1("CPACR_EL1", 0b11, 0b000, 0b0001, 0b0000, 0b010);
constexpr SysReg CPTR_EL2("CPTR_EL2", 0b11, 0b100, 0b0001, 0b0001, 0b010);
constexpr SysReg CPTR_EL3("CPTR_EL3", 0b11, 0b110, 0b0001, 0b0001, 0b010);
constexpr SysReg FPCR("FPCR", 0b11, 0b011, 0b0100, 0b0100, 0b000);
constexpr SysReg FPSR("FPSR", 0b11, 0b011, 0b0100, 0b0100, 0b001);
constexpr SysReg RNDR("RNDR", 0b11, 0b011, 0b0010, 0b0100, 0b000);
constexpr SysReg RNDRRS("RNDRRS", 0b11, 0b011, 0b0010, 0b0100, 0b001);
constexpr SysReg SCXTNUM_EL0("SCXTNUM_EL0", 0b11, 0b011, 0b1101, 0b0000, 0b111);
constexpr SysReg SCXTNUM_EL1("SCXTNUM_EL1", 0b11, 0b000, 0b1101, 0b0000, 0b111);
constexpr SysReg SCXTNUM_EL2("SCXTNUM_EL2", 0b11, 0b100, 0b1101, 0b0000, 0b111);
constexpr SysReg SCXTNUM_EL3("SCXTNUM_EL3", 0b11, 0b110, 0b1101, 0b0000, 0b111);
constexpr SysReg CNTVCT_EL0("CNTVCT_EL0", 0b11, 0b011, 0b1110, 0b0000, 0b010);
constexpr SysReg CNTPCT_EL0("CNTPCT_EL0", 0b11, 0b011, 0b1110, 0b0000, 0b001);
constexpr SysReg CNTVOFF_EL2("CNTVOFF_EL2", 0b11, 0b100, 0b1110, 0b0000, 0b011);
constexpr SysReg CNTKCTL_EL1("CNTKCTL_EL1", 0b11, 0b000, 0b1110, 0b0001, 0b000);
constexpr SysReg CNTHCTL_EL2("CNTHCTL_EL2", 0b11, 0b100, 0b1110, 0b0001, 0b000);
constexpr SysReg CNTV_CTL_EL0("CNTV_CTL_EL0", 0b11, 0b011, 0b1110, 0b0011, 0b001);
constexpr SysReg CNTV_CVAL_EL0("CNTV_CVAL_EL0", 0b11, 0b011, 0b1110, 0b0011, 0b010);
constexpr SysReg FAR_EL1("FAR_EL1", 0b11, 0b000, 0b0110, 0b0000, 0b000);
constexpr SysReg FAR_EL2("FAR_EL2", 0b11, 0b100, 0b0110, 0b0000, 0b000);
constexpr SysReg FAR_EL3("FAR_EL3", 0b11, 0b110, 0b0110, 0b0000, 0b000);
constexpr SysReg HPFAR_EL2("HPFAR_EL2", 0b11, 0b100, 0b0110, 0b0000, 0b100);
constexpr SysReg ESR_EL1("ESR_EL1", 0b11, 0b000, 0b0101, 0b0010, 0b000);
constexpr SysReg ESR_EL2("ESR_EL2", 0b11, 0b100, 0b0101, 0b0010, 0b000);
constexpr SysReg ESR_EL3("ESR_EL3", 0b11, 0b110, 0b0101, 0b0010, 0b000);
constexpr SysReg ZCR_EL1("ZCR_EL1", 0b11, 0b000, 0b0001, 0b0010, 0b000);
constexpr SysReg ZCR_EL2("ZCR_EL2", 0b11, 0b100, 0b0001, 0b0010, 0b000);
constexpr SysReg ZCR_EL3("ZCR_EL3", 0b11, 0b110, 0b0001, 0b0010, 0b000);
constexpr SysReg ID_AA64ZFR0_EL1("ID_AA64ZFR0_EL1", 0b11, 0b000, 0b0000, 0b0100, 0b100);
constexpr SysReg APDAKeyHi_EL1("APDAKEYHI_EL1", 0b11, 0b000, 0b0010, 0b0010, 0b001);
constexpr SysReg APDAKeyLo_EL1("APDAKEYLO_EL1", 0b11, 0b000, 0b0010, 0b0010, 0b000);
constexpr SysReg APDBKeyHi_EL1("APDBKEYHI_EL1", 0b11, 0b000, 0b0010, 0b0010, 0b011);
constexpr SysReg APDBKeyLo_EL1("APDBKEYLO_EL1", 0b11, 0b000, 0b0010, 0b0010, 0b010);
constexpr SysReg APGBKeyHi_EL1("APIGKEYHI_EL1", 0b11, 0b000, 0b0010, 0b0011, 0b001);
constexpr SysReg APGBKeyLo_EL1("APIGKEYLO_EL1", 0b11, 0b000, 0b0010, 0b0011, 0b000);
constexpr SysReg APIAKeyHi_EL1("APIAKEYHI_EL1", 0b11, 0b000, 0b0010, 0b0001, 0b001);
constexpr SysReg APIAKeyLo_EL1("APIAKEYLO_EL1", 0b11, 0b000, 0b0010, 0b0001, 0b000);
constexpr SysReg APIBKeyHi_EL1("APIBKEYHI_EL1", 0b11, 0b000, 0b0010, 0b0001, 0b011);
constexpr SysReg APIBKeyLo_EL1("APIBKEYLO_EL1", 0b11, 0b000, 0b0010, 0b0001, 0b010);

// Other encodings in the System instruction encoding space
//                            name                  op0    op1    CRn     CRm     op2
constexpr SysInst CFP_RCTX("CFP_RCTX", /*0b01*/ 0b011, 0b0111, 0b0011, 0b100);
constexpr SysInst CPP_RCTX("CPP_RCTX", /*0b01*/ 0b011, 0b0111, 0b0011, 0b111);
constexpr SysInst DVP_RCTX("DVP_RCTX", /*0b01*/ 0b011, 0b0111, 0b0011, 0b101);

} // namespace archx
} // namespace ampere

#endif // AMPERE_ARCHX_SYSREG_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
