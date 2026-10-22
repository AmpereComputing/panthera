#!/bin/bash -eux
# Copyright (c) 2021 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


rootfs_tgz=alpine-minirootfs-3.8.1-aarch64.tar.gz
rootfs_url="http://dl-cdn.alpinelinux.org/alpine/v3.8/releases/aarch64/$rootfs_tgz"
rootfs_image="$PWD/alpine_rootfs.qcow2"
rootfs_unpacked="$PWD/rootfs"
rootfs_mountpoint="$PWD/rootfs_mount"
rootfs_size="250G"
mkdir -p $rootfs_unpacked $rootfs_mountpoint

# Get and unpack rootfs tarball
wget -c $rootfs_url
sudo tar --warning=no-unknown-keyword -C $rootfs_unpacked/ -xf $rootfs_tgz

################################################################################
# Edit this section of the script to copy any workload-specific scripts or
# files to $rootfs_unpacked before it is used to create the rootfs. It is handy
# to create scripts to call the workload for you, so you can later specify
# `init=/path/to/run/script.sh` on the kernel command-line to run different
# workloads without needing to enter an interactive shell. An example:

#sudo cp -r ./local/path/to/my_workload $rootfs_unpacked
#sudo cat <<EOF > $rootfs_unpacked/run.sh
##!/bin/sh
#cd /opt/my_workload
#./run_workload ./input_file >& workload.log
#EOF
#sudo chmod +x $rootfs_unpacked/run.sh
################################################################################

# Create empty qcow2 image
qemu-img create -f qcow2 -o nocow=on $rootfs_image $rootfs_size

# Use nbd to mount the qcow image in loopback mode and create ext4 rootfs from
# contents of $rootfs_unpacked
sudo modprobe nbd
sudo qemu-nbd --connect /dev/nbd0 $rootfs_image
sudo mkfs.ext4 -d $rootfs_unpacked -L rootfs /dev/nbd0
sudo qemu-nbd --disconnect /dev/nbd0

# Shrink/compress image
mv $rootfs_image ${rootfs_image}.prev
qemu-img convert -O qcow2 -o nocow=on -c ${rootfs_image}.prev $rootfs_image
rm -f ${rootfs_image}.prev
