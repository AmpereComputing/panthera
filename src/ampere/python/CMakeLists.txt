# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

install(DIRECTORY panthera
        DESTINATION lib/python
        PATTERN "*/schema/*" EXCLUDE
        PATTERN "*.pyc" EXCLUDE
        PATTERN "__pycache__/*" EXCLUDE)

add_subdirectory(test)
