#!/bin/bash -x
# Copyright (c) 2024-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


    panthera=$(realpath ${PWD}/../../../../../)
      kernel=${KERNEL_IMAGE:-Image}
    qcow_img=alpine.qcow2
      binary=pagetouch
     initcmd='/pagetouch 40000 1 -r -w'
         mem=16G
 bbvinterval=10000000
 chkinterval=100000000
   tracebase=data-vector
mavblocksize=4096

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
	--trace-mav \
	--mav-blocksize ${mavblocksize} \
	--use-hint-instructions \
	-- ${initcmd}

$panthera/install/bin/panthera tracing.mav_parse ${tracebase}.generate_bbvs.mav --self-similarity > ${tracebase}.generate_bbvs.mav.sim
$panthera/install/bin/panthera tracing.mav_parse ${tracebase}.generate_bbvs.mav --sum-unique-address > ${tracebase}.generate_bbvs.mav.sum

#echo
#$panthera/install/bin/panthera tracing.bbv_matrix --combine-bbvs 1 ${tracebase}.generate_bbvs.bbv > ${tracebase}_bbv.csv
#
#echo
#$panthera/install/bin/panthera tracing.plot_similarity_matrix --workload "${tracebase}" --image-filename ${tracebase}_ss.png --instructions-per-interval "100 millions" ${tracebase}_bbv.csv 
