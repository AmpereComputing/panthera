#!/bin/bash
# Copyright (c) 2021-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


   panthera=${PANTHERA_ROOT:-../panthera}
     kernel=${KERNEL_IMAGE:-Image}
   qcow_img=ubuntu_Sep2020.img
     binary=abc
  tracebase=`basename $PWD`
#    initcmd=/bin/sh
    initcmd=/root/cmds/${tracebase}.sh
        mem=8G
bbvinterval=10000000
chkinterval=1000000000

echo $tracebase

qemu-system-aarch64 \
	-smp 1   \
	-M virt   \
	-cpu max   \
	-m ${mem}   \
	-icount 0    \
	-nographic    \
	-semihosting   \
	-kernel $kernel \
	-append "console=ttyAMA0 root=/dev/vda1 rw perf.semihosting init=${initcmd} earlycon=smh" \
	-drive if=none,id=image,file=${qcow_img},format=qcow2 \
	-device virtio-blk-pci,drive=image \
	-plugin $panthera/install/lib/libbbv_generator.so,arg="--profile-comm ${binary} --bbv-interval ${bbvinterval} --checkpoint-interval ${chkinterval} --bbv-filename tracing.bbv --generation-log tracing_bbv.json" \
	-d plugin \
	-D tracing.log

$panthera/src/ampere/tracing_tools/scripts/bbv_matrix.py --combine-bbvs 10 tracing.bbv > ${tracebase}_bbv.csv

$panthera/src/ampere/tracing_tools/scripts/plot_similarity_matrix.py ${tracebase}_bbv.csv --workload "${tracebase}" --image-filename ${tracebase}_ss.png --instructions-per-interval "100 millions"
