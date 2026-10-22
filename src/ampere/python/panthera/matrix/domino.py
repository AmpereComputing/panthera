# Copyright (c) 2023-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Define some components used in the Domino product.

Here is the most basic example of how to render something in a Jupyter Notebook:

    import sys
    sys.path.append('/path/to/panthera/install/lib/python')

    from matrix.domino import CCL
    from IPython.display import SVG

    SVG(CCL().render())

    ===> diagram should be produced <===
"""

import panthera.lynx as lynx

from .components import *


class CCM2(CpxComplex):
    """A 2-Core Complex"""

    DEVICES = [Core] * 2


class CCM4(CpxComplex):
    """A 4-Core Complex"""

    DEVICES = [Core] * 4


class PxHA2(CpxComplex):
    """A 2-PxHA complex."""

    DEVICES = [PxHA] * 2


class PxHA3(CpxComplex):
    """A 3-PxHA complex."""

    DEVICES = [PxHA] * 3


class PxHA4(CpxComplex):
    """A 4-PxHA complex."""

    DEVICES = [PxHA] * 4


class PmHA2(CpxComplex):
    """A 2-PmHA complex."""

    DEVICES = [PmHA] * 2


class PmHA3(CpxComplex):
    """A 2-PmHA complex."""

    DEVICES = [PmHA] * 3


class PmHA4(CpxComplex):
    """A 4-PmHA complex."""

    DEVICES = [PmHA] * 4


class PHAxxm(CpxComplex):
    """A 2-PxHA, 1-PmHA complex."""

    DEVICES = [PxHA, PxHA, PmHA]


class MCU(CpxComplex):
    """Two memory sub-channels in an MCU."""

    DEVICES = [MemSubChan] * 2


class ACR_COM(CpxComplex):
    DEVICES = [CCL_ACR_DDR, COM]


class IORc(CpxComplex):
    DEVICES = [IOR, CXL]


class IORW(CpxComplex):
    DEVICES = [IOR]


class IOHc(CpxComplex):
    DEVICES = [IOH, IOD_ACR_SMMU]


class MHA_ACR_SYS(CpxComplex):
    DEVICES = [MHA_SYS, CCL_ACR_SYS]


class MHA_ACR_GIC(CpxComplex):
    DEVICES = [MHA_GIC, CCL_ACR_GIC]


class NHP_MHAD(CpxComplex):
    DEVICES = [NHP, MHA_DVM]


class MCU_ACR(CpxComplex):
    """Two memory sub-channels in an MCU."""

    DEVICES = [MemSubChan, MemSubChan, CCL_ACR_DDR]


class IORr_CXL(CpxComplex):
    DEVICES = [IORr, CXL]


class IOHr_ACR_MMU(CpxComplex):
    DEVICES = [IOD_ACR_SMMU, IOHr]


class IOD_SYS(CpxComplex):
    DEVICES = [IOD_ACR_SYS, MHA_SYS]


class IOD_GIC(CpxComplex):
    DEVICES = [IOD_ACR_SYS, MHA_SYS]


class CCL(Die):
    # Note: In reality the MHA, ACR and ADIs in the top and bottom rows share port 1
    # through a CPX. I've put them on separate ports here so as not to have to change
    # any code around connecting ADIs by name.
    TILES = [
        [
            [CCM4, (ADI, "ADI_7"), MHA_ACR_GIC, None],
            [CCM4, PHAxxm, (ADI, "ADI_8"), None],
            [(ADI, "ADI_9"), NHP, (ADI, "ADI_10"), None],
            [CCM4, PHAxxm, (ADI, "ADI_11"), None],
            [CCM4, MCU, None, None],
        ],
        [
            [CCM4, (ADI, "ADI_6"), None, None],
            [CCM4, PHAxxm, None, None],
            [PHAxxm, PHAxxm, None, None],
            [CCM4, PHAxxm, None, None],
            [CCM4, CCL_ACR_DDR, None, None],
        ],
        [
            [CCM4, (ADI, "ADI_5"), None, None],
            [CCM4, PHAxxm, None, None],
            [PHAxxm, PHAxxm, None, None],
            [CCM4, PHAxxm, None, None],
            [CCM4, CCL_ACR_DDR, None, None],
        ],
        [
            [CCM4, (ADI, "ADI_4"), MHA_ACR_SYS, None],
            [CCM4, PHAxxm, (ADI, "ADI_0"), None],
            [(ADI, "ADI_1"), NHP_MHAD, (ADI, "ADI_2"), None],
            [CCM4, PHAxxm, (ADI, "ADI_3"), None],
            [CCM4, MCU, None, None],
        ],
    ]


class IOD(Die):
    TILES = [
        [
            [(ADI, "ADI_8"), None, None, None],
            [IORr_CXL, IOHr_ACR_MMU, None, None],
            [(ADI, "ADI_10"), None, None, None],
            [IORw, IOHw, None, None],
            [IORw, IOHw, None, None],
            [(ADI, "ADI_13"), None, None, None],
            [IORr_CXL, IOHr_ACR_MMU, None, None],
            [(ADI, "ADI_15"), None, None, None],
        ],
        [
            [IORw, IOHw, None, None],
            [(ADI, "ADI_9"), IOD_ACR_MTRACE, None, None],
            [IORr_CXL, IOHr_ACR_MMU, None, None],
            [(ADI, "ADI_11"), MHA_DVM, None, None],
            [(ADI, "ADI_12"), None, None, None],
            [IORr_CXL, IOHr_ACR_MMU, None, None],
            [(ADI, "ADI_14"), IOD_ACR_MTRACE, None, None],
            [IORw, IOHw, None, None],
        ],
        [
            [IORr_CXL, IOHr_ACR_MMU, None, None],
            [(ADI, "ADI_1"), None, None, None],
            [IORw, IOHw, None, None],
            [(ADI, "ADI_3"), None, None, None],
            [(ADI, "ADI_4"), None, None, None],
            [IORw, IOHw, None, None],
            [(ADI, "ADI_6"), None, None, None],
            [IORr_CXL, IOHr_ACR_MMU, None, None],
        ],
        [
            [(ADI, "ADI_0"), None, None, None],
            [IORw, IOHw, None, None],
            [(ADI, "ADI_2"), IOD_ACR_MTRACE, None, None],
            [IORr_CXL, IOHr_ACR_MMU, IOD_SYS, None],
            [IORr_CXL, IOHr_ACR_MMU, IOD_GIC, None],
            [(ADI, "ADI_5"), IOD_ACR_MTRACE, None, None],
            [IORw, IOHw, None, None],
            [(ADI, "ADI_7"), None, None, None],
        ],
    ]


class Domino(SoC):
    """Die layout:

    --------------+--------------
                 4|7
                 5|6
         1        |     3
                 6|5
    11 10  9  8  7|4   0  1  2  3
    --------------+--------------
     3  2  1  0  4|7   8  9 10 11
                 5|6
         2        |     4
                 6|5
    11 10  9  8  7|4   0  1  2  3
    --------------+--------------
     8  9 10 11       12 13 14 15

                  0

     0  1  2  3        4  5  6  7
    --------------+--------------
     3  2  1  0  4|7   8  9 10 11
                 5|6
         5        |     7
                 6|5
    11 10  9  8  7|4   0  1  2  3
    --------------+--------------
     3  2  1  0  4|7   8  9 10 11
                 5|6
         6        |     8
                 6|5
                 7|4
    --------------+--------------
    """

    DIES = [
        {"die_id": 1, "die_type": CCL, "x_off": 0, "y_off": 0, "rotate": 180},
        {"die_id": 2, "die_type": CCL, "x_off": 0, "y_off": 4, "rotate": 180},
        {"die_id": 3, "die_type": CCL, "x_off": 5, "y_off": 0},
        {"die_id": 4, "die_type": CCL, "x_off": 5, "y_off": 4},
        {"die_id": 0, "die_type": IOD, "x_off": 1, "y_off": 8},
        {"die_id": 5, "die_type": CCL, "x_off": 0, "y_off": 12, "rotate": 180},
        {"die_id": 6, "die_type": CCL, "x_off": 0, "y_off": 16, "rotate": 180},
        {"die_id": 7, "die_type": CCL, "x_off": 5, "y_off": 12},
        {"die_id": 8, "die_type": CCL, "x_off": 5, "y_off": 16},
    ]

    CONNECTIONS = [
        # Horizontal connections; they are all the same for each pair of CCLs
        ((1, "ADI_4"), (3, "ADI_7")),
        ((1, "ADI_5"), (3, "ADI_6")),
        ((1, "ADI_6"), (3, "ADI_5")),
        ((1, "ADI_7"), (3, "ADI_4")),
        ((2, "ADI_4"), (4, "ADI_7")),
        ((2, "ADI_5"), (4, "ADI_6")),
        ((2, "ADI_6"), (4, "ADI_5")),
        ((2, "ADI_7"), (4, "ADI_4")),
        ((5, "ADI_4"), (7, "ADI_7")),
        ((5, "ADI_5"), (7, "ADI_6")),
        ((5, "ADI_6"), (7, "ADI_5")),
        ((5, "ADI_7"), (7, "ADI_4")),
        ((6, "ADI_4"), (8, "ADI_7")),
        ((6, "ADI_5"), (8, "ADI_6")),
        ((6, "ADI_6"), (8, "ADI_5")),
        ((6, "ADI_7"), (8, "ADI_4")),
        # Vertical connections
        ((1, "ADI_11"), (2, "ADI_3")),
        ((1, "ADI_10"), (2, "ADI_2")),
        ((1, "ADI_9"), (2, "ADI_1")),
        ((1, "ADI_8"), (2, "ADI_0")),
        ((3, "ADI_0"), (4, "ADI_8")),
        ((3, "ADI_1"), (4, "ADI_9")),
        ((3, "ADI_2"), (4, "ADI_10")),
        ((3, "ADI_3"), (4, "ADI_11")),
        ((5, "ADI_11"), (6, "ADI_3")),
        ((5, "ADI_10"), (6, "ADI_2")),
        ((5, "ADI_9"), (6, "ADI_1")),
        ((5, "ADI_8"), (6, "ADI_0")),
        ((7, "ADI_0"), (8, "ADI_8")),
        ((7, "ADI_1"), (8, "ADI_9")),
        ((7, "ADI_2"), (8, "ADI_10")),
        ((7, "ADI_3"), (8, "ADI_11")),
        # IOD connections
        ((2, "ADI_11"), (0, "ADI_8")),
        ((2, "ADI_10"), (0, "ADI_9")),
        ((2, "ADI_9"), (0, "ADI_10")),
        ((2, "ADI_8"), (0, "ADI_11")),
        ((4, "ADI_0"), (0, "ADI_12")),
        ((4, "ADI_1"), (0, "ADI_13")),
        ((4, "ADI_2"), (0, "ADI_14")),
        ((4, "ADI_3"), (0, "ADI_15")),
        ((5, "ADI_3"), (0, "ADI_0")),
        ((5, "ADI_2"), (0, "ADI_1")),
        ((5, "ADI_1"), (0, "ADI_2")),
        ((5, "ADI_0"), (0, "ADI_3")),
        ((7, "ADI_8"), (0, "ADI_4")),
        ((7, "ADI_9"), (0, "ADI_5")),
        ((7, "ADI_10"), (0, "ADI_6")),
        ((7, "ADI_11"), (0, "ADI_7")),
    ]


class Domino_Octant(Domino):
    DIES = [{"die_id": 3, "die_type": CCL, "x_off": 0, "y_off": 0}]


class Domino_Quad(Domino):
    DIES = [
        {"die_id": 2, "die_type": CCL, "x_off": 0, "y_off": 0, "rotate": 180},
        {"die_id": 4, "die_type": CCL, "x_off": 5, "y_off": 0},
    ]


class Domino_Hemi(Domino):
    DIES = [
        {"die_id": 1, "die_type": CCL, "x_off": 0, "y_off": 0, "rotate": 180},
        {"die_id": 2, "die_type": CCL, "x_off": 0, "y_off": 4, "rotate": 180},
        {"die_id": 3, "die_type": CCL, "x_off": 5, "y_off": 0},
        {"die_id": 4, "die_type": CCL, "x_off": 5, "y_off": 4},
        {"die_id": 0, "die_type": IOD, "x_off": 1, "y_off": 8},
    ]


class Domino_Octant1_IO(Domino):
    DIES = [
        {"die_id": 1, "die_type": CCL, "x_off": 0, "y_off": 0, "rotate": 180},
        {"die_id": 2, "die_type": CCL, "x_off": 0, "y_off": 4, "rotate": 180},
        {"die_id": 0, "die_type": IOD, "x_off": 1, "y_off": 8},
    ]


class Domino_Octant2_IO(Domino):
    DIES = [
        {"die_id": 3, "die_type": CCL, "x_off": 5, "y_off": 0},
        {"die_id": 4, "die_type": CCL, "x_off": 5, "y_off": 4},
        {"die_id": 0, "die_type": IOD, "x_off": 1, "y_off": 8},
    ]


class Domino_Octant3_IO(Domino):
    DIES = [
        {"die_id": 0, "die_type": IOD, "x_off": 1, "y_off": 8},
        {"die_id": 5, "die_type": CCL, "x_off": 0, "y_off": 12, "rotate": 180},
        {"die_id": 6, "die_type": CCL, "x_off": 0, "y_off": 16, "rotate": 180},
    ]


class Domino_Octant4_IO(Domino):
    DIES = [
        {"die_id": 0, "die_type": IOD, "x_off": 1, "y_off": 8},
        {"die_id": 7, "die_type": CCL, "x_off": 5, "y_off": 12},
        {"die_id": 8, "die_type": CCL, "x_off": 5, "y_off": 16},
    ]


class Domino_IOD_Only(Domino):
    DIES = [
        {"die_id": 0, "die_type": IOD, "x_off": 0, "y_off": 0},
    ]


class DefaultRules(lynx.Rules):
    """Provide default routing rules for Domino.

    This includes some basic knowledge about which components can talk to others, as well as how
    the dies are interconnected. It does not include any XY overrides or other special rules. The
    idea is that those will be provided by a subclass.

    One (sort-of) exception to the above is that we supply a routing scheme for the die level. It
    is (mostly) Y-first, but allows crossing through the IOD.
    """

    def __init__(self, model):
        super().__init__()
        self.max_dies = 9
        self.all_dies_yfirst()
        self.connect_dies(model)

    def all_dies_yfirst(self):
        for die_id in range(self.max_dies):
            self.set_yfirst(die_id)

    # The following table describes how to reach a target die from a given source die. The path
    # includes the intermediate dies, but not the final target. The layout below is basically
    # "Y-first" between dies, but allows crossing to the other side inside the IOD if the target
    # is on the opposite side from the source. This saves an ADI hop for such paths.
    #
    # Die layout:
    #
    #     1 | 3
    #     2 | 4
    #       0
    #     5 | 7
    #     6 | 8
    #
    MULTI_DIE_PATHS = {
        (0, 1): [2],
        (0, 3): [4],
        (0, 6): [5],
        (0, 8): [7],
        (1, 0): [2],
        (1, 4): [2],
        (1, 5): [2, 0],
        (1, 6): [2, 0, 5],
        (1, 7): [2, 0],
        (1, 8): [2, 0, 7],
        (2, 3): [1],
        (2, 5): [0],
        (2, 6): [0, 5],
        (2, 7): [0],
        (2, 8): [0, 7],
        (3, 0): [4],
        (3, 2): [4],
        (3, 5): [4, 0],
        (3, 6): [4, 0, 5],
        (3, 7): [4, 0],
        (3, 8): [4, 0, 7],
        (4, 1): [3],
        (4, 5): [0],
        (4, 6): [0, 5],
        (4, 7): [0],
        (4, 8): [0, 7],
        (5, 1): [0, 2],
        (5, 2): [0],
        (5, 3): [0, 4],
        (5, 4): [0],
        (5, 8): [6],
        (6, 0): [5],
        (6, 1): [5, 0, 2],
        (6, 2): [5, 0],
        (6, 3): [5, 0, 4],
        (6, 4): [5, 0],
        (6, 7): [5],
        (7, 1): [0, 2],
        (7, 2): [0],
        (7, 3): [0, 4],
        (7, 4): [0],
        (7, 6): [8],
        (8, 0): [7],
        (8, 1): [7, 0, 2],
        (8, 2): [7, 0],
        (8, 3): [7, 0, 4],
        (8, 4): [7, 0],
        (8, 5): [7],
    }

    def connect_dies(self, model):
        have_die = [False] * self.max_dies

        # First, connect everything that the SoC model has direct links for.
        for from_comp, to_comp in model.all_connections():
            have_die[from_comp.die_id] = True
            have_die[to_comp.die_id] = True
            assert from_comp.die_id != to_comp.die_id
            self.add_cross_die_route(from_comp.die_id, to_comp.die_id, [from_comp])

        # Now connect following the rules in MULTI_DI_PATHS.
        for (from_die, to_die), path in self.MULTI_DIE_PATHS.items():
            if have_die[from_die] and have_die[to_die]:
                self.connect_die_transitively(from_die, to_die, *path)

    def connect_die_transitively(self, src, tgt, *vias):
        """Connect die `src` to die `tgt` by proceeding through the list of `vias`,
        which are intermediate waypoint dies.

        Currently we do not allow vertical routes to change columns in an intermediate
        die (this is what is meant by "criss-cross"). This is done by detecting and
        pruning those possibilities.
        """
        for route in self._all_routes([src] + list(vias) + [tgt], []):
            if self._vertical_route_does_not_criss_cross(route):
                self.add_cross_die_route(src, tgt, route)

    def _vertical_route_does_not_criss_cross(self, route):
        """Given a route, which is a list of ADI components, return True if the route
        does not change columns while travelling vertically.

        This is made more complex than it should be based on the fact that our input
        route is a list of ADIs which are needed to *exit* the die they are on, but
        it does not include the ADI through which we *enter* the next die. So first
        we build an `expanded_route` which includes the other side of the ADI
        connection.
        """
        adi_far_side = {(a.die_id, a.x, a.y): b for a, b in self.model.all_connections()}
        expanded_route = []
        for adi in route:
            expanded_route.append(adi)
            expanded_route.append(adi_far_side[(adi.die_id, adi.x, adi.y)])

        last_adi = None
        for adi in expanded_route:
            if last_adi is not None:
                if adi.die_id == last_adi.die_id:
                    if adi.die_id != 0:
                        if adi.x != 0 and last_adi.x != 0 and adi.x != last_adi.x:
                            return False
            last_adi = adi
        return True

    def _all_routes(self, path, pending_route):
        if len(path) < 2:
            yield pending_route
        else:
            for route in self.cross_die_routes(path[0], path[1]):
                yield from self._all_routes(path[1:], pending_route + route)
