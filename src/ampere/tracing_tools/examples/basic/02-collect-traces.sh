#!/bin/bash
# Copyright (c) 2022-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


    panthera=../panthera
    basename=basic
    kernel=${KERNEL_IMAGE:-Image}

${panthera}/install/bin/panthera tracing.trace \
    --output=${basename}                       \
    --rootfs-hda=alpine.qcow2                  \
    -kernel ${kernel}                          \
   -- basic

