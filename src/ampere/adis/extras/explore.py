#!/usr/bin/env python3
#
# Copyright (c) 2022 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
from __future__ import annotations

vals = [ 0x0530a000,
         0x052a8000,
         0x05288000,
         0x0531a000,
         0x052b8000,
         0x05298000,
         0x05218000,
         0x0528a000,
         0x05208000,
         0x0520a000,
         0x05228000,
         0x0521a000,
         0x05238000,
         0x05278000,
         0x05248000,
         0x05258000,
         0x05268000,
         0x052e8000,
         0x052d8000,
         0x052c8000 ]




def find_highest_nonzero_bit_list(l):
    def find_highest_nonzero_bit(x):
        for i in range(32):
            p = 1 << i
            if p&x:
                return i
        return 32
                       
    m = min([find_highest_nonzero_bit(x) for x in l])
    return m



def work():
    shift = find_highest_nonzero_bit_list(vals)
    print(shift)
    vshift =  [v >> shift for v in vals]
    for v,v2 in zip(vals,vshift):
        print(f'{v:08x}, {v2:08x}')
    
if __name__ == "__main__":
    work()
        
