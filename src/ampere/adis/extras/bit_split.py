#!/usr/bin/env python3
#
# Copyright (c) 2021 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
"""
When preparing the encodings, the code recursively splits (put into
bins) encodings based on fixed bits (1s or 0s).  Sometimes that
doesn't work and we need two backup strategies:

  1) split based on not-equal constraints like "!=0000" or "!=011".

  2) split based on splattering dontcare-bits that align with fixed
    1/0 bits.  Example: Splattering '1xx' would give the binary
    numbers 4 through 7.
"""

from __future__ import annotations
import copy
import collections


import util
import bitgather
import instr
def find_lowest_nonzero_bit(x):
    for i in range(32):
        p = 1 << i
        if p&x:
            return i
    return 32

def find_highest_nonzero_bit(x):
    for i in range(32):
        p = 1 << 31-i
        if p&x:
            return 31-i
    return 0
def count_ones(y):
    return bin(y).count('1')


def find_universal_mask(lde: list[instr.decorated_encoding_t]) -> int:
    universal_mask = int('1' * 32, 2)
    for de in lde:
        universal_mask = universal_mask & de.enc.required_mask_num()
    return universal_mask


def find_difference_constant_mask(lde: list[instr.decorated_encoding_t]) -> int:
    """Return mask of 1s, a subset of fixed bits mask, where the bit
    values differ across that bitpos in the input. Ignore bits that do
    not vary."""
    universal_mask = int('1' * 32, 2)
    for de in lde:
        universal_mask = universal_mask & de.enc.required_mask_num()

    # find the required bitpos in the universal mask
    required_bits = []
    for i,b in enumerate(list(util.bin32(universal_mask))): # bit 31 is first iteration...
        if b == '1':
            required_bits.append(31-i)

    # build d, a dictionary of sets of the values of required bits for each thing in lde.
    d  = {}  # dict indexed by bitpos with values, contains sets
    for rb in required_bits:
        d[rb] = set([])
        for de in lde:
            b = de.enc.get_bit(rb) # return ternbits, but should be 0 or 1 (as str)
            if b in ['0','1']:
                d[rb].add(b)

    # any set in d with 2 values we want to keep the corresponding index bit.
    # those sets correspond to "intereseting" bits..

    # any set in d with 1 value  we want to ignore as they are all the same.
    keep_bitpos = []
    for rb,v in d.items():
        if len(v)==2:
            keep_bitpos.append(rb)

    # make a new mask with 1s where we have bitpos values in keep_bitpos.
    out_mask = 0
    for rb in keep_bitpos:
        out_mask = out_mask | (1 << rb)

    return out_mask


def find_universal_forbid_mask(lde):
    universal_mask = int('1' * 32, 2)
    for de in lde:
        if de.enc.requires_forbid_test():
            universal_mask = universal_mask & de.enc.forbid_mask_num()
    return universal_mask


def check_dontcares_vs_fixed_bits(lde: list[instr.decorated_encoding_t],
                                  level: int) -> collections.defaultdict(list):
    """when dontcares align with fixed bits, splatter the dontcares
    and excluded the corresponding composite fixed 0/1 bit values from
    that splatter. The splattering can generate many new bins if there
    are many dontcare bits aligning with fixed 0/1 bits."""

    pad = "   " * level
    for de1 in lde:
        possible_dc = ~ de1.enc.required_mask_num()
        if possible_dc == 0:  # no dontcares in de1
            continue
        required = int('1' * 32, 2)
        for de2 in lde:
            if de1 == de2:
                continue
            required = required & de2.enc.required_mask_num()

        # c is the overlap between dontcares in de1 and required bits
        # in everything else. c is a subset of a required bits mask.
        c = required & possible_dc
        # print(f'{c=:#010x}')

        # looking at ARM v9, generally one of the 'c' values will be
        # nonzero if this technique is going to work. Sometimes it
        # doesn't work at all (one case).
        if c:
            # we want to replicate and modify de1, replacing dontcares
            # corresponding to the nonzeros in c with the values that
            # are not used by the others.
            c_bin = util.bin32(c)
            # print(c_bin)
            nbits = c_bin.count('1')
            nvals = 1 << nbits
            bitpos = []  # bit position 0 is on right!
            # record where the 1s are in the c mask
            for i, b in enumerate(c_bin):
                if b == '1':
                    bitpos.append(31 - i)
            # print(bitpos)

            # make a set called dset, of all the new things we'll be
            # replacing for the dontcares by doing a distributed bit
            # count into d and adding them to dset. We expand the
            # dense binary encoding in v_bin to the correct bit
            # positions in d.
            dset = set([])
            for v in range(0, nvals):
                v_bin = f'{v:0{nbits}b}'   # convert v to binary with enough bits. do not call bin()! result is too short.
                d = list(c_bin)            # split it into bits so we can index into it and replace stuff
                for n in range(0, nbits):   # replace the 1 bits  in c with 0/1 values from v.
                    d[31 - bitpos[n]] = v_bin[nbits - n - 1]
                dset.add(int("".join(d), 2))  # convert binary to normal integer
            # print('\tbefore removals : ' + " ".join([ f'{x:#010x}' for x in dset]))

            # now we remove any of the values from dset that already
            # existed in our input instructions.

            for de2 in lde:
                if de1 == de2:
                    continue
                existing_pattern = de2.enc.required_bits_num() & c  # c is a subset of a reqd bit mask
                if existing_pattern in dset:
                    dset.remove(existing_pattern)
            # print('\tafter   removals : ' + " ".join([f'{x:#010x}' for x in dset]))

            # now we have to replicate de1, modify the required masks
            # & required values according to the values from dset, put
            # it back in the list and resplit this pile

            # We want to set the bitpos[] bits in de1.enc to the
            # values from the bitpos[] location each dset member

            new_lde = []
            for v in dset:
                v_str = util.bin32(v)
                de1_clone = copy.deepcopy(de1)
                for pos in bitpos:
                    de1_clone.enc.set_bit(pos, v_str[31 - pos])
                new_lde.append(de1_clone)

            # collect up the stuff from lde, minus de1
            for de in lde:
                if de != de1:
                    new_lde.append(de)

            print(pad, "NEW STUFF:")
            for de in new_lde:
                print(pad, f'{de.inst.attrib}')
                de.enc.print_fields(pad)
                de.enc.print_masks(pad)

            # See if we can split the new_lde normally
            bins = collections.defaultdict(list)
            #universal_mask = find_universal_mask(new_lde)
            universal_mask = find_difference_constant_mask(new_lde) # 2021-10-05 EXPERIMENT
            # see if we can split the new_lde normally
            for de in new_lde:
                obin = de.enc.apply_required_mask(universal_mask)
                bins[obin].append(de)
            nbins = len(bins)
            print(pad, f"alt split into nbins={nbins}")
            if nbins == 1:
                print(pad, "Splattering dontcares did not split the bin")
            print(pad, f"Mask used in splattering: {universal_mask:08x}")
            return bins, universal_mask

    return None, None


def try_forbid_splittest(lde: list[instr.decorated_encoding_t],
                         level: int) -> collections.defaultdict(list):
    """use the "!=00000"  (or whatever value) fields to split in to 2
    bins(eq,ne)."""
    global gstats

    pad = "   " * level
    bins = collections.defaultdict(list)

    forbid_mask = find_universal_forbid_mask(lde)
    assert forbid_mask != 0  # complete disjoint forbid masks.. yikes... die

    # make sure forbid mask is same for all things
    forbid_values = set([])
    for de in lde:
        if de.enc.requires_forbid_test():
            # we will make sure they are all the same or give up..
            bins['ne'].append(de)
            if de.enc.forbid_mask_num() != forbid_mask:
                print(pad, "Disjoint forbid-masks...giving up")
                gstats.disjoint_values_in_forbid_masks += 1
                return None
            forbid_values.add(de.enc.forbid_bits_num())

    if len(forbid_values) != 1:
        print(pad, "Multiple forbid values...giving up")
        return None

    forbid_value = list(forbid_values)[0]
    # now to place the others... make sure they match the forbid_value
    for de in lde:
        if not de.enc.requires_forbid_test():
            if de.enc.required_bits_num() & forbid_mask == forbid_value:
                bins['eq'].append(de)
            else:
                bins['ne'].append(de)

    return bins


def try_alternative_strategies(lde: list[instr.decorated_encoding_t],
                               level: int) -> collections.defaultdict(list):
    """There are two (complex) fallback strategies:

    (1) use the "!=00000"  (or whatever value) fields to split in to 2
    bins(eq,ne).

    (2) when dontcares align with fixed bits, splatter the dontcares
    and exclude the corresponding composite fixed 0/1 bit values from
    that splatter. The splattering can generate many new bins if there
    are many dontcare bits aligning with fixed 0/1 bits."""
    #if {x for x in lde if x.enc.requires_forbid_test()}:
    if any([x.enc.requires_forbid_test() for x in lde]):
        return try_forbid_splittest(lde, level), 'ne-test', None
    bins, splatter_mask = check_dontcares_vs_fixed_bits(lde, level)
    return bins, 'splatter', splatter_mask


def bin_nodes(args,
              lde: list[instr.decorated_encoding_t],
              cumulative_mask: int = 0,
              level: int = 0):
    """Attempt recursive decode by looking for fixed common bits and
    distributing to bins based on those fixed bits.

   decode algorithm:
       given a list of instructions
       find a universal mask, a descriminator.
       put the instructions into bins based on that mask.
       for any bin with 0 or 1 instructions, we are done.  apply any remaining mask bits.
       for any bin with more than 1 instruction, recur
                 (we can ignore the bits processed already by cumulative universal mask)

    Sometimes that doesn't work because of not-equal constraints like
    (!=0000) or when we have fixed bits aligning with dontcares. We
    employ two complex fallback strategies for those cases. See the
    function "try_alternative_strategies"

    If the instructions are decodable, this process is guaranteed to terminate"""

    def _lde_print(lde):
        for i,de in enumerate(lde):
            print(pad, f'{i}: {de.inst.attrib}')
            de.enc.print_fields(pad)
            de.enc.print_masks(pad)

    def _no_overlap_print(level, nodes, cumulative_mask, lde):
        print(pad, f"NO OVERLAPPING REQUIRED BITS: level={level} nodes={nodes} cumulative_mask={cumulative_mask:#010x}")

    def _alt_split_print(lde):
        print(pad, 'trying alt split...')
        _lde_print(lde)

    def _print_bin_result(universal_mask, nbins, level, nodes, cumulative_mask):
        maxbins = 1 << count_ones(universal_mask)
        if maxbins > nbins:
            c = '>'
        else:
            c = '=='
        print(pad, f'level={level} nodes={nodes} cumulative_mask={cumulative_mask:#010x}  universal_mask={universal_mask:#010x} maxbins={maxbins} {c} nbins={nbins}')

    def _one_bin_print(lde):
        print(pad, "ONE BIN")
        _lde_print(lde)

    def _binfo_print(kind, bins, tmask=0):
        if kind == 'ne-test':
            print("BININFO: ne-test")
        else:
            ones = count_ones(tmask)
            hibit = find_highest_nonzero_bit(tmask)
            lobit = find_lowest_nonzero_bit(tmask)
            diff = hibit - lobit
            _, nmask, bitpos = bitgather.bit_difference(bins.keys(), tmask)
            hibit_nmask = find_highest_nonzero_bit(nmask)
            print(f"BININFO: {kind} mask={tmask:08x} hibit={hibit} lobit={lobit} diff={diff} ones={ones} nmask={nmask:08x} hibit_nmask={hibit_nmask:08x} bitpos={bitpos}")

        i=0
        for k, v in bins.items():
            if kind == 'ne-test':
                print(f'{i} BININFO {k} {len(v)}')
            else:
                print(f'{i} BININFO {k:08x} {len(v)}')
            i = i + 1

    def _hash(args, bins, tmask):
        global gstats
        # compute hash functions for splitting stuff into the bins
        if args.hash:
            import hash
            f = hash.find_hash(bins.keys(), tmask)
            if f.p:
                print(f'found {f}')
            else:
                print('not found')
                gstats.could_not_find_hash_function += 1

            if f.time > gstats.max_time_to_compute_hash:
                gstats.max_time_to_compute_hash = f.time


    global gstats
    pad = "   " * level
    universal_mask = find_universal_mask(lde)
    gstats.masks[universal_mask] += 1
    nodes = len(lde)
    assert nodes > 1
    # turn off any bits already in the cumulative mask
    universal_mask = universal_mask & ~cumulative_mask
    if universal_mask == 0:
        _no_overlap_print(level, nodes, cumulative_mask, lde)
        gstats.no_matching_required_bits += 1
    else:
        assert universal_mask != 0
        cumulative_mask |= universal_mask

    bins = collections.defaultdict(list)
    for de in lde:
        obin = de.enc.apply_required_mask(universal_mask)
        bins[obin].append(de)
    nbins = len(bins)

    kind='default'
    if nbins == 1:
        _alt_split_print(lde)
        f  = try_alternative_strategies(lde, level)
        if f:
            bins,kind,splatter_mask = f
            if bins:
                nbins = len(bins)
                assert nbins >= 2
        else:
            util.die("splatter failed")

    _print_bin_result(universal_mask, nbins, level, nodes, cumulative_mask)
    if nbins == 1:
        # failed to split input... error, print a diagnostic for debugging
        _one_bin_print(lde)
        util.die("all strategies failed")

    tmask = universal_mask
    if kind != 'ne-test':
        if kind == 'splatter':
            tmask = splatter_mask
        # compute hash functions for splitting stuff into the bins
        _hash(args, bins, tmask)
    _binfo_print(kind, bins, tmask)

    # recursively split the newly-created bins containing > 1 item.
    for _, v in bins.items():
        if len(v) > 1:  # more than one thing in the bin, gotta split it...
            assert level <= 10
            bin_nodes(args, v, cumulative_mask, level + 1)  # recur
