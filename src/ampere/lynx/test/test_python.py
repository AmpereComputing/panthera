# Copyright (c) 2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import unittest

from panthera.lynx import *


def add_tile(model: Model, die_id: int, x: int, y: int, devs=()):
    model.add_component(Component(ComponentType.RTR, Location(die_id, x, y)))
    for port, d in enumerate(devs):
        if d is None:
            continue
        dtype = getattr(ComponentType, d)
        model.add_component(Component(dtype, Location(die_id, x, y, port)))


def setup_mesh1x1(model: Model, die_id: int = 0):
    add_tile(model, die_id, 0, 0, ("Core", "PxHA", "PmHA", "MCU"))


def setup_mesh2x1(model: Model, die_id: int = 0):
    add_tile(model, die_id, 0, 0, ("Core",))
    add_tile(model, die_id, 1, 0, ("PxHA", "PxHA", "PmHA", "MCU"))


def setup_mesh2x2(model: Model, die_id: int = 0):
    add_tile(model, die_id, 0, 0, ("Core", "PxHA"))
    add_tile(model, die_id, 0, 1, ("Core", "PxHA"))
    add_tile(model, die_id, 1, 1, ("Core", "PmHA", "MCU"))
    add_tile(model, die_id, 1, 0, ("Core", "PmHA", "MCU", "ADI"))


class Traffic(unittest.TestCase):
    """These are equivalent to the C++ tests in `test_traffic.cc`.

    We just want to ensure the same functionality is available through Python."""

    def setUp(self):
        self.mesh1x1 = Model()
        self.mesh2x1 = Model()
        self.mesh2x2 = Model()
        setup_mesh1x1(self.mesh1x1)
        setup_mesh2x1(self.mesh2x1)
        setup_mesh2x2(self.mesh2x2)
        self.rules = Rules()
        self.acc = Accumulator()

    def traffic_on_link(self, opcode, comps):
        return self.acc.traffic_on_link(self.rules.channel_map(opcode), comps)[opcode]

    def test_ReadSharedToMemoryMesh1x1(self):
        core = self.mesh1x1.find_component(Location(0, 0, 0, 0))
        pxha = self.mesh1x1.find_component(Location(0, 0, 0, 1))
        pmha = self.mesh1x1.find_component(Location(0, 0, 0, 2))
        mcu = self.mesh1x1.find_component(Location(0, 0, 0, 3))
        rtr = self.mesh1x1.find_component(Location(0, 0, 0))

        core.ReadShared(self.mesh1x1, self.rules, 100.0, False, self.acc)

        self.assertEqual(self.traffic_on_link(Opcode.ReadShared, (core, rtr)), 100.0)
        self.assertEqual(self.traffic_on_link(Opcode.ReadShared, (rtr, pxha)), 100.0)
        self.assertEqual(self.traffic_on_link(Opcode.ReadShared, (pxha, rtr)), 100.0)
        self.assertEqual(self.traffic_on_link(Opcode.ReadShared, (rtr, pmha)), 100.0)
        self.assertEqual(self.traffic_on_link(Opcode.ReadNoSnp, (pmha, rtr)), 100.0)
        self.assertEqual(self.traffic_on_link(Opcode.ReadNoSnp, (rtr, mcu)), 100.0)

    def test_ReadSharedToMemoryMesh2x1(self):
        core = self.mesh2x1.find_component(Location(0, 0, 0, 0))
        pxha0 = self.mesh2x1.find_component(Location(0, 1, 0, 0))
        pxha1 = self.mesh2x1.find_component(Location(0, 1, 0, 1))
        rtr0 = self.mesh2x1.find_component(Location(0, 0, 0))
        rtr1 = self.mesh2x1.find_component(Location(0, 1, 0))

        # Inject traffic load of 100% ReadShared from the core
        core.ReadShared(self.mesh2x1, self.rules, 100.0, False, self.acc)

        self.assertEqual(self.traffic_on_link(Opcode.ReadShared, (core, rtr0)), 100.0)
        self.assertEqual(self.traffic_on_link(Opcode.ReadShared, (rtr0, rtr1)), 100.0)
        self.assertEqual(self.traffic_on_link(Opcode.ReadShared, (rtr1, pxha0)), 50.0)
        self.assertEqual(self.traffic_on_link(Opcode.ReadShared, (rtr1, pxha1)), 50.0)

        self.assertEqual(self.traffic_on_link(Opcode.ReadShared, (pxha0, rtr1)), 50.0)
        self.assertEqual(self.traffic_on_link(Opcode.ReadShared, (pxha1, rtr1)), 50.0)

    def test_ReadSharedToMemoryMesh2x2(self):
        # Inject traffic load of 100% ReadShared from the core in the upper left corner
        src = self.mesh2x2.find_component(Location(0, 0, 1, 0))
        src.ReadShared(self.mesh2x2, self.rules, 100.0, False, self.acc)

        # We should see:
        #
        #     1) 50% ReadShared to the co-located PxHA
        #     2) 50% ReadShared to the PxHA at 0,0
        #     3) 25% ReadShared from 1,1 -> 1,0 and from 1,0 -> 1,1 because each PxHA's share
        #        will be split among the PmHAs and only half will flow north or south
        #
        traffic_to_colocated_pxha = self.traffic_on_link(
            Opcode.ReadShared, (self.mesh2x2.find_rtr(src.loc), self.mesh2x2.find_component(Location(0, 0, 1, 1)))
        )
        traffic_from_x0y1_to_x0y0 = self.traffic_on_link(
            Opcode.ReadShared, (self.mesh2x2.find_rtr(Location(0, 0, 1)), self.mesh2x2.find_rtr(Location(0, 0, 0)))
        )

        self.assertEqual(traffic_to_colocated_pxha, 50.0)
        self.assertEqual(traffic_from_x0y1_to_x0y0, 50.0)

        rtr10 = self.mesh2x2.find_rtr(Location(0, 1, 0))
        rtr11 = self.mesh2x2.find_rtr(Location(0, 1, 1))

        self.assertEqual(self.traffic_on_link(Opcode.ReadShared, (rtr10, rtr11)), 25.0)
        self.assertEqual(self.traffic_on_link(Opcode.ReadShared, (rtr11, rtr10)), 25.0)
