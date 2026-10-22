#!/bin/sh
# Copyright (c) 2020 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


$1 --help | grep -q "core.instruction_stream_name"
