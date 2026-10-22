# Copyright (c) 2010 by Veloce Technologies.  All Rights Reserved.
# SPDX-License-Identifier: BSD-3-Clause

# The Pattern class provides a method of manipulating a boolean
# algebra expression (a.k.a. a PLA-type truth table or a Karnaugh
# map).  An instance of the class records all combinations of inputs
# that make the output true.  The input is required to be an N-bit
# binary vector, and the output is a single binary bit.
#
# The class records the set of input values that make the output true.
# The input values are recorded in PLA style, as a set of OR'ed terms,
# where each term is a set of input bits AND'ed together.  E.g.
#   output = (input[2:0] == 0) | (input[2:0] == 2) | (input[2:0] >= 6)
# is recorded as
#   output = (~input[2] & ~input[0]) |
#            (input[2] & input[1])
#
# Each OR'ed term is recorded as a value and mask.  E.g.
#   output = ((input[2:0] & 0b101) == 0b000) |
#             (input[2:0] & 0b110) == 0b110)
#
# The values and masks are stored internally as integers, but are
# usually presented for human reading in Espresso format.  I.e.
# useful bits are output as binary digits and masked (don't-care) bits
# are output as "-".  E.g. "0-0 | 11-".
#
# If the output is always false, the Pattern is empty.  I.e. it has
# no terms.
#
# If the output is always true, the Pattern includes a single term
# with all bits masked off, e.g. "---".
#
# A Pattern instance is initialized with the number of bits in its
# input vector.  It can be initialized with a single term or as the
# empty set.  The pattern can then be combined with other patterns
# with AND or OR, or other binary logic can be performed on it.
#
# As binary logic is performed, the Pattern class attempts to keep the
# pattern in minimal form.  E.g. "110" OR "--1" yields "11- | --1".
# For complex patterns, the minimal form is neither obvious nor
# canonical.  (That's what Espresso is for.)  The result may differ
# depending on the order of operations or on the internal dictionary
# order of terms.  As an additional consequence, there is no easy way
# to determine whether two patterns are equivalent.
#
#
# The following pattern functions are available.
#
#
# Pattern manipulation with copy:
#
# copy() - return a copy of the pattern.
#
# p_copy_not() - return a copy of the pattern with the output value
# inverted.  I.e. the set of values and masks is modified to achieve
# the inverted output value.  It is computationally expensive to invert
# a complex pattern.
#
#
# Pattern manipulation in place:
#
# p_not() - the pattern is modified in place to invert the output
# value.  It is computationally expensive to invert a complex pattern.
# As a convenience, the pattern also returns itself.
#
# p_and() - the pattern is modified by ANDing it with a second
# pattern.  As a convenience, the pattern also returns itself.
#
# p_and_not() - the pattern is modified by ANDing it with the inverse
# of a second pattern.  It is computationally expensive to invert a
# complex pattern.  As a convenience, the pattern also returns itself.
#
# p_or() - the pattern is modified by ORing it with a second
# pattern.  As a convenience, the pattern also returns itself.
#
#
# Pattern queries:
#
# is_empty() - returns True if the pattern is empty (has no terms).
#
# p_intersects() - returns True if the output is true for any
# combination of input values for both the pattern and a second
# pattern.  This is equivalent to but more efficient than
# "not self.p_and(other).is_empty()".
#
#
# Pattern retrieval:
#
# p_list() - returns a list of (value, mask) tuples.
#
# p_format() - returns a list of Espresso-formatted terms.
#
# p_print() - prints a list of Espresso-formatted terms to stdout.


def c_cmp(p):
    return (p.val << p.size) | p.mask


def c_format(size, val, mask):
    # optimized since it is used so much
    return "".join([("1" if val & (1 << i) else "0") if mask & (1 << i) else "-" for i in reversed(range(size))])


class Pattern:
    TO_MASK = ''.maketrans('01-', '110')
    TO_VALUE = ''.maketrans('01-', '010')

    def __init__(self, size, val=None, mask=None):
        self.size = size
        if val is None:
            self.p = set()
        else:
            self.p = {(val, mask)}
            if val & ~mask:
                raise Exception("value (0x%08x) has bits set that are not in the mask (0x%08x)" % (val, mask))

    def add_pattern(self, p_str):
        assert len(p_str) == self.size
        value = int(p_str.translate(Pattern.TO_VALUE), 2)
        mask = int(p_str.translate(Pattern.TO_MASK), 2)
        self.p_add(value, mask)
        return self

    def is_empty(self):
        return len(self.p) == 0

    def copy(self):
        p = Pattern(self.size)
        p.p = self.p.copy()
        return p

    # Add a code to an existing pattern.
    # Discard unnecessary codes, and merge similar codes.
    def p_add(self, val, mask):
        # identical patterns may be common in the most complex and() and and_not() cases, so we take an early exit
        if (val, mask) in self.p:
            return

        remove_list = []
        for (val1, mask1) in self.p:
            if (mask & mask1 & val) == (mask & mask1 & val1):
                # The value at the intersection of the masks is compatible.
                # Is one mask a subset of the other?
                if ((mask ^ mask1) & ~mask) == 0:
                    # The places where the masks are different are entirely within the new mask. I.e. the patterns
                    # allowed by the new mask are a subset of the patterns allowed by the existing mask.
                    # There's no need to insert the new pattern.
                    return
                elif ((mask ^ mask1) & ~mask1) == 0:
                    # The places where the masks are different are entirely within the existing mask. I.e. the patterns
                    # allowed by the existing mask are a subset of the patterns allowed by the new mask.
                    # Remove the existing mask.
                    remove_list.append((val1, mask1))
            elif mask == mask1:
                # The masks are the same.
                # Check whether the new value is just one bit different from an existing value.
                diff = (mask & val) ^ (mask & val1)
                if not diff & (diff - 1):
                    # The new value is just one bit different from an existing value.
                    # Remove the existing value, then re-insert a code that merges the existing and new value.
                    # Recursion will continue until the entire cube of similar values are rolled up as don't cares.
                    self.p.remove((val1, mask1))
                    self.p_add(val & ~diff, mask & ~diff)
                    return
        for elem in remove_list:
            self.p.remove(elem)
        self.p.add((val, mask))

    def p_intersects(self, other):
        p1 = self.p
        p2 = other.p
        for (val1, mask1) in p1:
            for (val2, mask2) in p2:
                if (mask1 & mask2 & val1) == (mask1 & mask2 & val2):
                    return True
        return False

    def code_is_subset(self, val1, mask1, p2_code_list, i):
        while i < len(p2_code_list):
            val2, mask2 = p2_code_list[i]

            if val1 & mask2 == val2:
                if self.expanded_code_is_subset(val1, mask1, p2_code_list, i):
                    return True

            i += 1

        return False

    def expanded_code_is_subset(self, val1, mask1, p2_code_list, i):
        val2, mask2 = p2_code_list[i]

        if ~mask1 & mask2 == 0:
            return True

        for bit in range(self.size):
            i2 = 1 << bit
            if ~mask1 & mask2 & i2:
                if not self.code_is_subset(val1 | (~val2 & i2), mask1 | i2, p2_code_list, i + 1):
                    return False

        return True

    # Non-canonical patterns mean that this can't be a simple test.
    # And since it is used for complex patterns, invoking p_not().p_and() is too expensive.
    # So instead we use specially written code to determine if "self & ~other" gets any results,
    # in which case self is *not* a subset of other.
    def code_is_subset_old(self, val1, mask1, p2_code_list, i):
        if i > len(p2_code_list):
            raise Exception(
                "code_is_subset was called with i=%d but size of p2 list is only %d" % (i, len(p2_code_list)))
        if i == len(p2_code_list):
            # There are no codes left to eliminate the remaining non-subset.
            return False
        else:
            (val2, mask2) = p2_code_list[i]
            if val1 & mask1 & mask2 == val2 & mask1 & mask2:
                # The codes are compatible.
                this_one_matches = True
                for bitNum in range(self.size):
                    i2 = 1 << bitNum
                    if ~mask1 & mask2 & i2:
                        # There are more required bit values in mask2 than in mask1, so we have to check what happens
                        # to the subsets where val1/mask1 uses any of the individual disallowed bit values.
                        if not self.code_is_subset(val1 | (val2 & i2), mask1 | i2, p2_code_list, i + 1):
                            this_one_matches = False  # This does not look right. What is the logic behind this?
                # Each subcase was a subset, so val1/mask1 is a subset.
                if this_one_matches:
                    return True

            # The codes are not compatible, or if they were, it could not be satisfied by the remainder of the list
            # So keep looking at the remainder of the list for the complete pattern.
            # val1/mask1 is only a subset of p2 if it is a subset of the remainder of p2.
            return self.code_is_subset(val1, mask1, p2_code_list, i + 1)

    def is_subset(self, other):
        p1 = self.p
        p2 = other.p
        p2_code_list = [x for x in p2]
        for (val1, mask1) in p1:
            if not self.code_is_subset(val1, mask1, p2_code_list, 0):
                return False
        return True

    def p_not(self, target=None):  # result is written to target (or self, if target == None) - see p_copy_not()
        # Sorting is very important when inverting something like "X.p_not() and Y.p_not()".
        # The sort makes sure that the related components are processed in an order that gets simplified as quickly as
        # possible. For less complicated cases, the sort runs plenty fast enough to not hurt performance.
        p1 = sorted(self.p)
        if target is None:
            target = self
            target.p = {(0, 0)}
        for (val, mask) in p1:
            codebar = Pattern(self.size)
            for i in range(self.size):
                i2 = 1 << i
                if mask & i2:
                    codebar.p.add((~val & i2, i2))
            target.p_and(codebar)
        return target

    def p_copy_not(self):
        return self.p_not(target=Pattern(self.size, 0, 0))

    def p_and(self, other, target=None):
        p1 = self.p
        p2 = other.p
        if target is None:
            target = self
            target.p = set()
        for (val1, mask1) in p1:
            for (val2, mask2) in p2:
                if (mask1 & mask2 & val1) == (mask1 & mask2 & val2):
                    # The value at the intersection of the masks is compatable.
                    # The result is the value required with the union of both masks.
                    target.p_add(val1 | val2, mask1 | mask2)
        return target

    def p_copy_and(self, other):
        return self.p_and(other, target=Pattern(self.size))

    def p_and_not(self, other):
        p1 = sorted(self.p)  # see sorting comment in p_not()
        p2 = sorted(other.p)
        not_other = None
        self.p = set()
        for (val1, mask1) in p1:
            intersects = False
            for (val2, mask2) in p2:
                if (mask1 & mask2 & val1) == (mask1 & mask2 & val2):
                    intersects = True
                    break
            if intersects:
                if not_other is None:
                    not_other = other.p_copy_not()
                self.p_or(Pattern(32, val1, mask1).p_and(not_other))
            else:
                # The codes do not intersect.  Therefore we can add the original code unchanged.
                self.p_add(val1, mask1)
        return self

    def p_or(self, other):
        p2 = other.p
        for (val2, mask2) in p2:
            self.p_add(val2, mask2)
        return self

    def p_list(self):
        return sorted(self.p)

    def p_format(self):
        # optimized since it is used so much
        return [c_format(self.size, val, mask) for (val, mask) in self.p_list()]

    def p_print(self, prefix, infix=None):
        if infix is None:
            infix = prefix
        return prefix + ("\n" + infix).join(self.p_format())
