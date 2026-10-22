#!/bin/bash -x
# Copyright (c) 2021-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


    panthera=$(realpath ${PWD}/../../../../../)
      kernel=${KERNEL_IMAGE:-Image}
    qcow_img=alpine.qcow2
      binary=nqueens20
     initcmd=/nqueens20
         mem=16G
 bbvinterval=10000000
 chkinterval=100000000
   tracebase=nqueens-20

echo $tracebase

${panthera}/install/bin/panthera tracing.generate_bbvs \
	--trace-children \
	--kernel ${kernel} \
	--rootfs ${qcow_img} \
	--rootfs-device "/dev/vda rw" \
	--output-prefix ${tracebase} \
	--bbv-interval ${bbvinterval} \
	--trace-process-name ${binary} \
	--checkpoint-interval ${chkinterval} \
	-m ${mem}  \
	-- ${initcmd}

echo
$panthera/install/bin/panthera tracing.bbv_matrix --combine-bbvs 10 ${tracebase}.generate_bbvs.bbv > ${tracebase}_bbv.csv

echo
$panthera/install/bin/panthera tracing.plot_similarity_matrix --workload "${tracebase}" --image-filename ${tracebase}_ss.png --instructions-per-interval "100 millions" ${tracebase}_bbv.csv 
