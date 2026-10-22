# Copyright (c) 2022-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import evp


def get_time(time):
    return {"time": time}


evp.collect(get_time)
