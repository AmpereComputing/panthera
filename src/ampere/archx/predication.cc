//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/archx/predication.h"

namespace ampere::archx {

static bool modify_predicate_info_multiple_memop(const std::unique_ptr<Uop>& uop, const pred_reg_t pval)
{
    const MultipleMemopType multiple = uop->multiple_memop();
    const unsigned sequence_num = uop->sequence_number(); // 0...N-1
    const unsigned esize = uop->vector_element_size();

    switch (multiple) {
    case MultipleMemopType::NONE:
        break;
    case MultipleMemopType::LD2:
    case MultipleMemopType::ST2: {
        /* Loading 2 x 16B = 32B
           for bytes:  bit 0 is used for first 2 bytes, bit 1 used for next 2 bytes, etc.
           for hwords: bit 0 is used for first 2 hwords, bit 2 is used for next two, etc. up to bit 14 for final 2
           hwords for words: bit 0 is used for first 2 words, bit 4 for next two words , bit 8 for next 2 words, and bit
           12 for next 2 words. for dwords: bit 0 is used for first 2 dwords, and bit 8 is used for 2nd 2 dwords.
        */
        unsigned mask_pattern = 0;
        switch (esize) {
        case 8:
            mask_pattern = 0x00FF;
            break;
        case 16:
            mask_pattern = 0x0055;
            break;
        case 32:
            mask_pattern = 0x0011;
            break;
        case 64:
            mask_pattern = 0x0001;
            break;
        default:
            AMPERE_ERROR("Need element size 8, 16, 32, or 64");
        }

        if ((pval & (mask_pattern << (sequence_num * 8))) == 0) {
            return true;
        }
        break;
    }

    case MultipleMemopType::LD3:
    case MultipleMemopType::ST3: {
        /*   bytes:     3rd load             2nd load            1st load
             |-------------------||-------------------||-------------------| loads
             ppp ooo nnn mmm lll kkk jjj iii hhh ggg fff eee ddd ccc bbb aaa
             15   14  13 12   11 10   9   8   7   6   5   4   3   2   1    0  predicate bit

             hwords:
             |----------------||-----------------||----------------| loads
             hhhhhh gggggg ffffff eeeeee dddddd cccccc bbbbbb aaaaaa
             14     12     10      8      6     4      2     0  predicate bit

             words:
             |-----------------| |-----------------| |-----------------| loads
             dddd.dddd.dddd cccc.cccc.cccc bbbb.bbbb.bbbb aaaa.aaaa.aaaa
             12              8            4             0             predicate bit

             dwords:
             |---------------| |---------------| |---------------|   loads
             bbbbbbbb.bbbbbbbb.bbbbbbbb aaaaaaaa.aaaaaaaa.aaaaaaaa
             8                           0                predicate bit         */
        unsigned rbits = 0; // relevant bits
        switch (esize) {
        case 8:
            switch (sequence_num) {
            case 0:
                rbits = pval & 0b0000'0000'0011'1111;
                break;
            case 1:
                rbits = pval & 0b0000'0111'1110'0000;
                break;
            case 2:
                rbits = pval & 0b1111'1100'0000'0000;
                break;
            default:
                AMPERE_ERROR("Need sequence number  0,1 or 2");
            }
            break;
        case 16:
            switch (sequence_num) {
            case 0:
                rbits = pval & 0b0000'0000'0001'0101;
                break;
            case 1:
                rbits = pval & 0b0000'0101'0101'0000;
                break;
            case 2:
                rbits = pval & 0b0101'0100'0000'0000;
                break;
            default:
                AMPERE_ERROR("Need sequence number  0,1 or 2");
            }
            break;
        case 32:
            switch (sequence_num) {
            case 0:
                rbits = pval & 0b0000'0000'0001'0001;
                break;
            case 1:
                rbits = pval & 0b0000'0001'0001'0000;
                break;
            case 2:
                rbits = pval & 0b0001'0001'0000'0000;
                break;
            default:
                AMPERE_ERROR("Need sequence number  0,1 or 2");
            }
            break;
        case 64:
            switch (sequence_num) {
            case 0:
                rbits = pval & 0b0000'0000'0000'0001;
                break;
            case 1:
                rbits = pval & 0b0000'0001'0000'0001;
                break;
            case 2:
                rbits = pval & 0b0000'0001'0000'0000;
                break;
            default:
                AMPERE_ERROR("Need sequence number  0,1 or 2");
            }
            break;
        default:
            AMPERE_ERROR("Need element size 8, 16, 32, or 64");
        }

        if (rbits == 0) { // if all the relevant bits are zero, the memop is predicated off
            return true;
        }
        break;
    }
    case MultipleMemopType::LD4:
    case MultipleMemopType::ST4: {
        /*   bytes:
             15   14   13   12     11   10   9    8       7   6     5    4      3   2     1    0     predicate bit
             pppp oooo nnn mmmm   llll kkkk jjjj iiii   hhhh gggg ffff eeee    dddd cccc bbbb aaaa
             4th load               3rd load              2nd load                 1st load
             hwords:
             14   14   12   12     10   10    8    8      6    6    4    4      2    2    0    0
             hhhh hhhh gggg gggg   ffff ffff eeee eeee   dddd dddd cccc cccc   bbbb bbbb aaaa aaaa
             words:
             12  12  12   12       8   8     8    8      4    4   4    4       0    0     0     0
             dddd dddd dddd dddd   cccc cccc cccc cccc    bbbb bbbb bbbb bbbb    aaaa aaaa aaaa aaaa

             dwords - single bits associated with more than one load!
             8           8         8          8          0        0          0        0
             bbbb.bbbb bbbb.bbbb   bbbb.bbbb bbbb.bbbb    aaaa.aaaa aaaa.aaaa aaaa.aaaa aaaa.aaaa         */
        unsigned mask_pattern = 0;
        switch (esize) {
        case 8:
            mask_pattern = 0x000F;
            break;
        case 16:
            mask_pattern = 0x0005;
            break;
        case 32:
            mask_pattern = 0x0001;
            break;
        case 64:
            mask_pattern = 0;
            break; // special handling required
        default:
            AMPERE_ERROR("Need element size 8, 16, 32, or 64");
        }

        if (mask_pattern) {
            if ((pval & (mask_pattern << (sequence_num * 4))) == 0) {
                return true;
            }
        } else { // dwords
            if ((sequence_num < 2 && (pval & 1) == 0) || (sequence_num >= 2 && (pval & 256) == 0)) {
                return true;
            }
        }
        break;
    }
    } // switch
    return false; // not predicated off
}

bool is_predicated_off(std::shared_ptr<ampere::archx::Instruction>& instruction, const std::unique_ptr<Uop>& uop)
{
    // Set uop->predicated_off to true if either: (1) the trace says
    // the uop is predicated off or (2) if the relevant bits in
    // predicate register value (which comes from the instruction
    // state) indicate that none of the relevant predicate bits are
    // enabled for this operation.

    if (!uop->is_predicated())
        return false;

    archx::LReg pred_reg_name;
    if (uop->is_ixu() || uop->is_lsu())
        pred_reg_name = uop->src3;
    else
        pred_reg_name = uop->pred;

    if (!archx::is_predicate_strict(pred_reg_name)) {
        return false; // just a a safety check to avoid FFR, a predicate-like register
    }

    const pred_reg_t pval = instruction->state().read_pred(pred_reg_name);
    const unsigned esize = uop->vector_element_size();

    // only handling predicated off for memops now.
    if (uop->is_gather() || uop->is_scatter() || uop->multiple_memop() != MultipleMemopType::NONE) {
        const unsigned sequence_num = uop->sequence_number(); // 0...N-1
        // tracer can set predicated off too...
        if (instruction->memop_predicated_off(sequence_num)) {
            return true;
        }

        // consider gathers, scatters, ld/st multiple...
        if (uop->is_gather() || uop->is_scatter()) {
            if (!archx::get_pred_bit(pval, sequence_num, esize)) {
                return true;
            }
            return false;
        } else if (uop->multiple_memop() != MultipleMemopType::NONE) {
            return modify_predicate_info_multiple_memop(uop, pval);
        }
    } else if (uop->is_lsu()) {
        // tracer could set realize these are predicated off
        if (instruction->memop_predicated_off(0)) {
            return true;
        }
    }

    if (archx::pred_all_zero(pval, esize)) { // normal SVE operations and single ld/st
        // all relevant bits are zero, so we know this uop is "off"
        return true;
    }

    return false;
}

} // namespace ampere::archx
