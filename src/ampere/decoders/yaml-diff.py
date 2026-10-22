#!/usr/bin/env python3
# Copyright (c) 2022-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


# diff two yaml files and point out changes relevant to panthera

import yaml
import argparse

def process_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('files',
                        help="Files to compare",
                        nargs='+')
    args = parser.parse_args()
    return args

def main():
    args = process_args()
    assert len(args.files) == 2
    yml = {}
    u = []
    for fn in args.files:
        with open(fn,'r') as f:
            yml[fn] = yaml.load(f,Loader=yaml.CLoader)
        u.append(yml[fn]['uops'])

    new_uops = set(u[1].keys()) - set(u[0].keys())
    s = "\n\t".join(sorted(list(new_uops)))
    print(f"new_uops:\n\t{s}")
    removed_uops = set(u[0].keys()) - set(u[1].keys())
    s = "\n\t".join(sorted(list(removed_uops)))
    print(f"removed_uops:\n\t{s}")

    common_uops = set(u[0].keys()) & set(u[1].keys())

    ignore_keys = set(['SVE_cat','Asimd_cat','SVE2_cat','InstSubSet', 'uopcode',
                       'Implemented'])

    icount = 0
    common_uops = sorted(common_uops)
    for uop in common_uops:
        ud0 = u[0][uop]
        ud1 = u[1][uop]
        if ud0 != ud1:
            s  = []
            k1 = set(ud0.keys())
            k2 = set(ud1.keys())
            new_keys = k2 - k1
            removed_keys = k1 - k2
            common_keys = k1 & k2
            for rk in removed_keys - ignore_keys:
                s.append(f"\tremoved\t{rk}:\t{ud0[rk]} -> nothing")
            for nk in new_keys - ignore_keys:
                s.append(f"\tnew    \t{nk}:\tnothing -> {ud1[nk]}")
            for ck in common_keys - ignore_keys:
                if ud0[ck] != ud1[ck]:
                    s.append(f"\tchange\t{ck}:\t{ud0[ck]} -> {ud1[ck]}")

            if s:
                found_reg = False
                for line in s:
                    if 'reg' in line and 'pred_reg' not in line:
                        found_reg = True
                        break
                if found_reg:
                    print(f'{icount}: {uop}:')
                    icount += 1
                    s = sorted(s)
                    for line in s:
                        if 'Producing_Uop' in line:
                            continue
                        if 'fsu_imm_bits' in line:
                            continue
                        if 'EarlyExit' in line:
                            continue
                        if 'pred_reg' in line:
                            continue
                        if 'pField' in line:
                            continue
                        if 'datasize' in line:
                            continue
                        if 'ARM_encname' in line:
                            continue
                        if 'ARM_iformname' in line:
                            continue
                        if 'imm_fields' in line:
                            continue
                        if 'Proposed' in line:
                            continue
                        if 'base_simp' in line:
                            continue
                        if 'base' in line:
                            continue
                        if 'pipe' in line:
                            continue
                        print(line)

if __name__ == "__main__":
    main()
