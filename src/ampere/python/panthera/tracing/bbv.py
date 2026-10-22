#!/usr/bin/env python3

#
# Copyright (c) 2018 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#

from collections.abc import MutableMapping

import numpy as np


class BBV(MutableMapping):
    def __init__(self, initial_vector=None):
        self._vector = initial_vector if initial_vector else {}

    def euclidean_distance(self, other):
        dimensions = sorted(list(set().union(self.keys(), other.keys())))

        sv = np.array([self[d] for d in dimensions])
        sv = sv / np.linalg.norm(sv)

        ov = np.array([other[d] for d in dimensions])
        ov = ov / np.linalg.norm(ov)

        return np.linalg.norm(ov - sv)

    def manhattan_distance(self, other):
        dimensions = sorted(list(set().union(self.keys(), other.keys())))

        sv = np.array([self[d] for d in dimensions])
        sv = sv / sv.sum()

        ov = np.array([other[d] for d in dimensions])
        ov = ov / ov.sum()

        return np.abs(ov - sv).sum()

    def magnitude(self):
        return sum(self._vector.values())

    # Emulate dictionary, except empty dimensions return 0
    def __len__(self):
        return self._vector.__len__()

    def __getitem__(self, key):
        try:
            return self._vector.__getitem__(key)
        except KeyError:
            return 0

    def __setitem__(self, key, value):
        if value == 0:
            self.__delitem__(key)
        else:
            self._vector.__setitem__(key, value)

    def __delitem__(self, key):
        try:
            self._vector.__delitem__(key)
        except KeyError:
            pass

    def __iter__(self):
        return iter(self._vector)

    # In-place addition with another BBV
    def __iadd__(self, other):
        assert isinstance(other, BBV), "Can only add to another BBV"
        for d in other:
            self[d] += other[d]
        return self

    def __add__(self, other):
        assert isinstance(other, BBV), "Can only add to another BBV"
        newbbv = BBV(self._vector.copy())
        return BBV.__iadd__(newbbv, other)

    def __imul__(self, other):
        for d in self:
            self[d] *= other
        return self

    def __mul__(self, other):
        newbbv = BBV(self._vector.copy())
        return BBV.__imul__(newbbv, other)

    def __rmul__(self, other):
        return BBV.__mul__(self, other)

    def __str__(self):
        return ", ".join(["0x%x: %d" % (k, self[k]) for k in self.keys()])

    def from_bbv_string(self, s):
        """
        Import a vector from a string taken from a simpoint-style BBV file.
        These are in the format:
            T:1:49 :2:494 :3:94 :4:182
        or
            P:0x5dd8dc:2 :0x50b284:938 :0x50b170:38

        Where the first character is the type prefix, and dimensions are
        space-delimited. The first number within each colon-delimited grouping
        is the dimension of the vector, and the second number is the magnitude
        (essentially 'frequency x length' of the basic block).

        Lines prefixed with 'P' are assumed to include full hexadecimal
        addresses as dimensions (i.e.  from the Plugin), while lines prefixed
        with 'T' are assumed to have decimal dimensions.
        """
        if s.startswith("T"):
            base = 10
        elif s.startswith("P"):
            base = 16
        else:
            raise Exception("Invalid BBV format")

        # Make sure we have a clean slate
        self._vector = {}

        for dim in s[1:].split():
            dims = dim.split(":", maxsplit=2)
            self[int(dims[1], base)] = int(dims[2])

    def to_bbv_string(self, hex_format=True):
        """
        Export vector as a string in the same format from_bbv_string imports
        """
        if hex_format:
            return "P" + " ".join([":0x%x:%d" % (k, self[k]) for k in self.keys()])
        else:
            return "T" + " ".join([":%d:%d" % (k, self[k]) for k in self.keys()])
