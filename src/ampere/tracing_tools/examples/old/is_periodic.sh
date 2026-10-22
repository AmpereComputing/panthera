#!/bin/bash
# Copyright (c) 2021-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


   panthera=${PANTHERA_ROOT:-../panthera}
     kernel=${KERNEL_IMAGE:-Image}
   qcow_img=centos8-SPEC17-TRAIN-gcc10.qcow2
     binary=leela_r_base.gcc10_rate_ampere-64
  tracebase=`basename $PWD`
     cmddir=`echo $tracebase | cut -d- -f1,2`
     cmdnum=`echo $tracebase | cut -d- -f3 | sed 's/train//g'`
    initcmd=/root/SpecINT-train/${cmddir}/cmd-${cmdnum}.sh
        mem=6G
 max_traces=2600
     period=100000000
 mem_warmup=100000000
inst_warmup=0
     length=100000000
   tracedir=is.$$

echo "Executing QEMU with init=${initcmd} with binary ${binary}"
echo "Collecting max ${max_traces} traces of length ${length} every ${period} instructions with ${mem_warmup} offline mem warmup"
sleep 3

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
	-append "console=ttyAMA0 root=/dev/vda3 rw perf.semihosting init=${initcmd} earlycon=smh" \
	-drive if=none,id=image,file=${qcow_img},format=qcow2 \
	-device virtio-blk-pci,drive=image \
	-plugin $panthera/install/lib/libinstruction_tracer.so,arg="--trace-filename-base ${tracebase} --max-traces ${max_traces} --profile-comm ${binary} --profile-children --start-tracing-periodically-after-instructions ${period} --stop-tracing-after-measuring-instructions ${length} --start-measuring-after-online-warmup-instructions ${inst_warmup} --start-online-warmup-after-offline-warmup-instructions ${mem_warmup} " \
    -d plugin \
	-D tracing.log

mkdir $tracedir
mv *.is.xz $tracedir
mv *.ms.xz $tracedir
