# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os.path

import panthera.help

exit(
    panthera.help.help(
        os.path.dirname(__file__), "panthera.", doc=panthera.help.__doc__, explicit={"study": "Run a python study"}
    )
)
