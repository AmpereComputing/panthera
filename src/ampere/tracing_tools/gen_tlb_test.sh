#!/bin/bash -x

# Copyright (c) 2019-2021 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

export rootfs_qcow=$PWD/image/rootfs.qcow2.img
export kernel_dir=~/projects/linux/arch/arm64/boot

~/projects/qemu/aarch64-softmmu/qemu-system-aarch64 \
	-smp 1 \
	-m 4G \
	-M virt \
	-cpu cortex-a57 \
	-kernel $kernel_dir/Image_4k \
	-append "console=ttyAMA0 root=/dev/vda rw norandmaps nokaslr init=/bin/sh earlycon=smh" \
	-drive if=none,file=$rootfs_qcow,id=hd0,format=qcow2 \
	-device virtio-blk-pci,drive=hd0 \
	-nographic \
	-semihosting \
	-plugin ~/projects/panthera/build/src/ampere/tracing_tools/libgen_tlb_test.so,arg="blah" \
	-d plugin \
	-D 4k_44bpa_tlb_test.inc \
	-icount 0
