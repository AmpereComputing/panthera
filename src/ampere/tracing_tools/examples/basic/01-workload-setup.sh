#!/bin/bash -x
# Copyright (c) 2022 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


    panthera=../panthera
      rootfs=http://dl-cdn.alpinelinux.org/alpine/v3.8/releases/aarch64/alpine-minirootfs-3.8.1-aarch64.tar.gz
    qcow_img=alpine.qcow2
      binary=basic
 rootfs_size=16G
   tracebase=basic 

${panthera}/install/bin/panthera tracing.create_image \
    --rootfs ${rootfs}                                \
    --output ${qcow_img}                              \
    --rootfs-size ${rootfs_size}                      \
    ${binary} /

