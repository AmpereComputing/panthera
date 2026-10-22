#!/bin/bash -x
# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


if [ "$#" -ne 1 ]; then
  echo "Error: You must pass Panthera path."
  echo "Usage: $0   "
  exit 1
fi
    panthera=$1
      kernel=Image-4k-v6.1-nosve
    qcow_img=ubuntu_Dec2022_node.img
   tracebase=`basename $PWD`
      binary=${tracebase}.sh
     initcmd=/sbin/init
         mem=64G
 bbvinterval=10000000
 chkinterval=10000000000

echo $tracebase
echo "Executing QEMU with init=${initcmd} to with binary ${binary}"

${panthera}/install/bin/panthera tracing.generate_bbvs \
        --trace-children \
        --kernel ${kernel} \
        --rootfs ${qcow_img} \
        --rootfs-device "/dev/vda2 rw" \
        --output-prefix ${tracebase} \
        --bbv-interval ${bbvinterval} \
        --trace-process-name ${binary} \
        --checkpoint-interval ${chkinterval} \
        --profile-el1 \
        -m ${mem} -cpu max,sve=off \
        -- ${initcmd}

$panthera/install/bin/panthera tracing.bbv_matrix --combine-bbvs 10 ${tracebase}.generate_bbvs.bbv > ${tracebase}_bbv.csv

$panthera/install/bin/panthera tracing.plot_similarity_matrix --workload "${tracebase}" --image-filename ${tracebase}_ss.png --instructions-per-interval "100 millions" ${tracebase}_bbv.csv
