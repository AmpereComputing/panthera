# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from .parallel import Parallel
from .printer import Printer
from .serial import Serial
from .slurm import Slurm

mapping = {
    "slurm": Slurm,
    "parallel": Parallel,
    "serial": Serial,
    "printer": Printer,
}
