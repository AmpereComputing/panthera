# Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import unittest
from math import sqrt

from panthera.tracing.bbv import BBV


class TestBBV(unittest.TestCase):
    def test_accessors(self):
        a = BBV()
        b = BBV()

        self.assertEqual(len(b), 0)
        b[0xABAB5C5C] = 8289
        self.assertEqual(b[0xABAB5C5C], 8289)
        self.assertEqual(len(b), 1)

        # Nonexistent dimensions return 0
        self.assertEqual(b[0x9], 0)

        # Setting key to 0 should delete it, but it should still return 0
        b[0xABAB5C5C] = 0
        self.assertEqual(len(b), 0)
        self.assertEqual(b[0xABAB5C5C], 0)

        # Deleting nonexistent key should silently work
        del b[0xABAB5C5C]

    def test_string_input_output(self):
        # Creation from strings
        a = BBV({5: 9})
        a.from_bbv_string("P:0xabab5c5c:8289 :0xaf87:9 :0x7:104")
        self.assertEqual(len(a), 3)
        self.assertEqual(a[0xABAB5C5C], 8289)
        self.assertEqual(a[0xAF87], 9)
        self.assertEqual(a[0x7], 104)
        self.assertEqual(a[5], 0)  # Ensure string import cleared vector

        a.from_bbv_string("T:928:8289 :9:9 :7:104")
        self.assertEqual(len(a), 3)
        self.assertEqual(a[928], 8289)
        self.assertEqual(a[9], 9)
        self.assertEqual(a[7], 104)
        self.assertEqual(a[0xAF87], 0)  # Ensure string import cleared vector

        # String output formatting
        self.assertEqual(a.to_bbv_string(hex_format=False), "T:928:8289 :9:9 :7:104")
        self.assertEqual(a.to_bbv_string(hex_format=True), "P:0x3a0:8289 :0x9:9 :0x7:104")

    def test_distance_calculations(self):
        # Do some basic sanity-checking of distance functions
        # Maximum distances
        a = BBV({0xA: 2, 0xB: 2})
        b = BBV({0xC: 100, 0xD: 9})
        self.assertAlmostEqual(a.euclidean_distance(b), sqrt(2), delta=10**-15)
        self.assertAlmostEqual(b.euclidean_distance(a), sqrt(2), delta=10**-15)
        self.assertAlmostEqual(a.manhattan_distance(b), 2.0, delta=10**-15)
        self.assertAlmostEqual(b.manhattan_distance(a), 2.0, delta=10**-15)

        # zero distances after normalization
        a = BBV({0xA: 2, 0xB: 2})
        b = BBV({0xA: 700, 0xB: 700})
        self.assertAlmostEqual(a.euclidean_distance(b), 0, delta=10**-15)
        self.assertAlmostEqual(b.euclidean_distance(a), 0, delta=10**-15)
        self.assertAlmostEqual(a.manhattan_distance(b), 0, delta=10**-15)
        self.assertAlmostEqual(b.manhattan_distance(a), 0, delta=10**-15)

        # Distance of one
        a = BBV({0xA: 5, 0xB: 5})
        b = BBV({0xA: 5, 0xC: 5})
        self.assertAlmostEqual(a.euclidean_distance(b), 1, delta=10**-15)
        self.assertAlmostEqual(b.euclidean_distance(a), 1, delta=10**-15)
        self.assertAlmostEqual(a.manhattan_distance(b), 1, delta=10**-15)
        self.assertAlmostEqual(b.manhattan_distance(a), 1, delta=10**-15)

        # Other distances
        a = BBV({0xA: 3, 0xB: 4})
        b = BBV({0xA: 4, 0xC: 3})
        self.assertAlmostEqual(
            a.euclidean_distance(b), sqrt((4.0 / 5 - 3.0 / 5) ** 2 + (4.0 / 5) ** 2 + (3.0 / 5) ** 2), delta=10**-15
        )
        self.assertAlmostEqual(
            b.euclidean_distance(a), sqrt((4.0 / 5 - 3.0 / 5) ** 2 + (4.0 / 5) ** 2 + (3.0 / 5) ** 2), delta=10**-15
        )
        self.assertAlmostEqual(a.manhattan_distance(b), 8.0 / 7, delta=10**-15)
        self.assertAlmostEqual(b.manhattan_distance(a), 8.0 / 7, delta=10**-15)

    def test_vector_math(self):
        a = BBV({0xA: 3, 0xB: 4})
        b = BBV({0xA: 4, 0xC: 3})

        # Addition to other vectors
        c = a + b
        self.assertEqual(c[0xA], 7)
        self.assertEqual(c[0xB], 4)
        self.assertEqual(c[0xC], 3)
        c2 = b + a
        self.assertEqual(c2[0xA], 7)
        self.assertEqual(c2[0xB], 4)
        self.assertEqual(c2[0xC], 3)

        # Multiplication against scalars
        d = c * 5
        self.assertEqual(d[0xA], 35)
        self.assertEqual(d[0xB], 20)
        self.assertEqual(d[0xC], 15)

        # Reverse and negative multiplication
        d = -5 * c
        self.assertEqual(d[0xA], -35)
        self.assertEqual(d[0xB], -20)
        self.assertEqual(d[0xC], -15)

        # In-place addition
        a += b
        self.assertAlmostEqual(c.manhattan_distance(a), 0, delta=10**-15)

        # In-place multiplication
        c *= 5
        self.assertAlmostEqual(c.manhattan_distance(d), 0, delta=10**-15)
