//
// Copyright (c) 2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef EXPORT_INSTR_H
#define EXPORT_INSTR_H
enum class InstClass : uint8_t {
    aluInstClass = 0,
    loadInstClass = 1,
    storeInstClass = 2,
    condBranchInstClass = 3,
    uncondDirectBranchInstClass = 4,
    uncondIndirectBranchInstClass = 5,
    fpInstClass = 6,
    slowAluInstClass = 7,
    undefInstClass = 8,
    callDirectInstClass = 9,
    callIndirectInstClass = 10,
    ReturnInstClass = 11
};

// Use pragma pack to control structure alignment
#pragma pack(push, 1) // Set alignment to 1 byte boundary
// Define a packed structure
struct exportedInstr
{
    uint64_t mPc;
    InstClass mType; // Type is InstClass
    bool mTaken;
    uint64_t mNextPc;
    uint64_t mEffAddr;
    uint8_t mMemSize; // In bytes
    uint8_t mBaseUpd;
    uint8_t mHasRegOffset;
    uint8_t mNumInRegs;
    std::vector<uint8_t> mInRegs;
    uint8_t mNumOutRegs;
    std::vector<uint8_t> mOutRegs;
    std::optional<uint8_t> mBaseUpdReg;
    std::vector<uint64_t> mOutRegsValues;

    exportedInstr() { reset(); }
    void print_exportedInstr() const
    {
        std::cout << "\nPC=0x" << std::hex << mPc;
        std::cout << "\tmEffAddr=0x" << std::hex << mEffAddr;
        std::cout << "\tmMemSize " << (uint32_t)mMemSize;
        std::cout << "\tmHasRegOffset= " << (uint32_t)mHasRegOffset;
        std::cout << "\nmNumInRegs= " << (unsigned int)mNumInRegs << std::endl;

        for (int i = 0; i < mNumInRegs; i++) {
            std::cout << "mInRegs[" << i << "]= " << (uint32_t)mInRegs[i] << ", ";
        }

        std::cout << "mNumOutRegs= " << (uint32_t)mNumOutRegs;
        for (int i = 0; i < mNumOutRegs; i++) {
            std::cout << "mOutRegs[" << i << "]= " << (uint32_t)mOutRegs[i] << ", ";
        }
        std::cout << std::endl;
        for (int i = 0; i < mNumOutRegs; i++) {
            std::cout << "mOutRegsValues[" << i << "]= " << (uint32_t)mOutRegsValues[i] << ", ";
        }
    }
    void reset()
    {
        mPc = mNextPc = 0xbaadf00d;
        mEffAddr = 0xbaadcafe;
        mMemSize = 0;
        mBaseUpdReg = 0;
        mHasRegOffset = 0;
        mType = InstClass::undefInstClass;
        mTaken = false;
        mNumInRegs = mNumOutRegs = 0;
        mInRegs.clear();
        mOutRegs.clear();
        mBaseUpdReg.reset();
        mOutRegsValues.clear();
    }
};
#pragma pack(pop) // Restore previous alignment settings
#endif            // EXPORT_INSTR
