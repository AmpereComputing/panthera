#!/bin/bash -x
# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


if [ "$#" -ne 1 ]; then
  echo "Error: You must pass Panthera path."
  echo "Usage: $0   "
  exit 1
fi

    panthera=$1
   tracebase=`basename $PWD`
  simpt_maxk=20
  simpt_bict=0.91

echo $tracebase

${panthera}/install/bin/panthera tracing.calculate_simpoints \
        --simpoint-maxk ${simpt_maxk} \
        --simpoint-bic-threshold ${simpt_bict} \
        --input-json ${tracebase}.generate_bbvs.json

echo
paste ${tracebase}.calculate_simpoints.weights ${tracebase}.calculate_simpoints.simpoints | sort -n
