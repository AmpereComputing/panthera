#!/usr/bin/env python3
#
# Copyright (c) 2022 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#

from __future__ import annotations
import collections

_test_mask_vals4 = 0x00d01c00
_test_case_vals4 = [
0x00000000,
0x00c00000,
0x00400000,
0x00800000,
0x00100400,
0x00d00400,
0x00500400,
0x00900400,
0x00100000,
0x00d00000,
0x00500000,
0x00900000,
0x00001800,
0x00101800,
0x00c01800,
0x00d01800,
0x00401800,
0x00501800,
0x00801800,
0x00901800,
0x00001000,
0x00101000,
0x00c01000,
0x00d01000,
0x00401000,
0x00501000,
0x00801000,
0x00901000,
0x00001c00,
0x00101c00,
0x00c01c00,
0x00d01c00,
0x00401c00,
0x00501c00,
0x00801c00,
0x00901c00,
0x00001400,
0x00101400,
0x00c01400,
0x00d01400,
0x00401400,
0x00501400,
0x00801400,
0x00901400]


_test_mask_vals5=0x00fffc1f
_test_case_vals5= [
0x003f0000,
0x003f081f,
0x003f0c1f,
0x001f0000,
0x001f081f,
0x001f0c1f,
0x00bf0000,
0x009f0000,
0x009f081f,
0x009f0c1f,
0x005f0000,
0x005f081f,
0x005f0c1f ]


def bin32(v):
    return f'{v:032b}'


def get_keeper_positions(msk:int) -> set:
    keeper_positions = set([])
    for i,v in enumerate(list(bin32(msk))):
        if v == '1':
            keeper_positions.add(31-i)
    return keeper_positions

def bit_gather(vin:int,msk:int) -> int:
    """Gather the 1/0s from vin corresponding to the 1s in msk. The
    bits are kept in the same relative order but that technically does
    not matter. This operation is also known as compress."""
    
    keeper_positions = get_keeper_positions(msk)
    cv = bit_gather2(vin,keeper_positions)
    #assert bin32(vin).count('1') == bin32(cv).count('1')
    return  cv

def bit_gather2(vin: int, keeper_positions:list) -> int:
    """Gather the 1/0s from vin corresponding to the indices in
    keeper_positions. The bits are kept in the same relative order but
    that technically does not matter. This operation is also known as
    compress."""
    
    cv = 0
    for i,v in enumerate(list(bin32(vin))):
        if 31-i in keeper_positions:
            cv = cv << 1 | (v == '1')

    return  cv



def bit_difference(vin_list: list, msk: int) -> (list, int, set):
    """using the msk, find all the varying bitpositions in
    vin_list. then bit-gather just the varying bits. return a new list
    of narrower (generally) data."""
    keeper_positions = get_keeper_positions(msk)
    bit_vary_tracker = collections.defaultdict(set)

    for vin in vin_list:
        vl = list(bin32(vin)) # split the value into ascii 1/0 bits (wrong order)
        for kbit in keeper_positions:
            vbit = vl[31-kbit] # fix bit order
            bit_vary_tracker[kbit].add(vbit)

    keeper_bitpos = set([])
    for bitpos,set_of_values in bit_vary_tracker.items():
        if len(set_of_values) == 2:
            keeper_bitpos.add(bitpos)

    # since the outputs are dense, we just need a dense low-aligned mask
    # corresponding to the number of bits we gathered
    nbits = len(keeper_bitpos)
    nmask = (1<<nbits)-1
    
    return [bit_gather2(x, keeper_bitpos) for x in vin_list], nmask, keeper_bitpos
    
    
class hrec_t:
    def __init__(self, v,msk):
        self.v = v
        self.msk = msk
        self.keeper_pos = get_keeper_positions(msk)
        self.gv = bit_gather(v, msk)

    def __str__(self):
        s = []
        s.append(f'v={self.v:08x}')
        s.append(f'msk={self.msk:08x}')
        s.append(f'gv={self.gv:08x}')
        s.append(f'keeper_pos={self.keeper_pos}')
        return " ".join(s)
        
        
def work(vs, msk):
    hs = [hrec_t(v,msk) for v in vs]
    for h in hs:
        print(h)

    print("Bit differened...")
    vout,nmsk,positions = bit_difference(vs,msk)
    hs = [hrec_t(v,nmsk) for v in vout]
    for h in hs:
        print(h)
    
    
if __name__ == "__main__":
    #work(_test_case_vals4, _test_mask_vals4)
    work(_test_case_vals5, _test_mask_vals5)

