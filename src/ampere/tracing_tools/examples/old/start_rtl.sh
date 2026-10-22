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
 max_traces=1200
     period=1000000000
 mem_warmup=10000000
inst_warmup=0
     length=200000
   tracedir=rtl

echo $tracebase

qemu-system-aarch64 \
	-smp 1   \
	-M virt   \
	-cpu max   \
	-m ${mem}   \
	-icount 0    \
	-nographic    \
	-semihosting   \
	-singlestep     \
	-kernel $kernel  \
	-append "console=ttyAMA0 root=/dev/vda1 rw perf.semihosting init=${initcmd} earlycon=smh" \
	-drive if=none,id=image,file=${qcow_img},format=qcow2 \
	-device virtio-blk-pci,drive=image \
	-plugin $panthera/install/lib/libinstruction_tracer.so,arg="--trace-filename-base ${tracebase} --max-traces ${max_traces} --profile-comm ${binary} --profile-children --start-tracing-periodically-after-instructions ${period} --stop-tracing-after-measuring-instructions ${length} --start-measuring-after-online-warmup-instructions ${inst_warmup} --start-online-warmup-after-offline-warmup-instructions ${mem_warmup} --trace-elf" \
	-d plugin \
	-D tracing.log

/usr/bin/parallel gzip ::: *.elf
mkdir $tracedir
mv *.is.xz $tracedir
mv *.ms.xz $tracedir
mv *.elf.gz $tracedir
cd $tracedir
# Copy the generated traces to the desired output location, if needed.
