#!/bin/bash
# Copyright (c) 2021-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


   panthera=${PANTHERA_ROOT:-../panthera}
     kernel=${KERNEL_IMAGE:-Image}
   qcow_img=ubuntu_media_2020.img
  tracebase=ffmpeg-h264-live-combined
    initcmd=/root/01-h264/ffmpeg_test.sh
        mem=20G
  runlength=20000000
  is_warmup=1000000
 mem_warmup=10000000
      suite=Media
   tracedir=is_traces

# run simpoints and capture traces
$panthera/src/ampere/tracing_tools/scripts/capture_traces.py \
    --generation-log tracing_bbv.json \
    --trace-base ${tracebase} \
    --trace-runlength ${runlength} \
    --start-measuring-after-online-warmup-instructions ${is_warmup} \
    --start-online-warmup-after-offline-warmup-instructions ${mem_warmup} \
    --plugin $panthera/install/lib/libinstruction_tracer.so \
    --qemu-command "qemu-system-aarch64 -smp 1 -M virt -cpu max -m ${mem} -icount 0 -nographic -semihosting -singlestep -kernel $kernel -append 'console=ttyAMA0 root=/dev/vda1 rw perf.semihosting init=${initcmd} earlycon=smh' -drive if=none,id=image,file=${qcow_img},format=qcow2 -device virtio-blk-pci,drive=image " || exit

# make tlist
$panthera/src/ampere/tracing_tools/scripts/manifest_to_workloads.py \
	${tracebase} manifest.json tracelist.json ${suite}

# move files to separate directory
mkdir $tracedir
mv *.is.xz $tracedir
mv *.ms.xz $tracedir
mv *.is.bbv $tracedir
mv tracelist.json $tracedir

# make disasm
cd $tracedir
for i in *.is.xz; do
    BASE=`echo ${i} | sed 's/is.xz/is/g'`
    ${panthera}/install/bin/is_disasm --e 30000000 ${i} > ${BASE}.dis && gzip ${BASE}.dis &
done
cd -
