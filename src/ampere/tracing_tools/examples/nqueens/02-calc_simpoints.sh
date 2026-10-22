#!/bin/bash -x
# Copyright (c) 2021 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


    panthera=$(realpath ${PWD}/../../../../../)
   tracebase=nqueens-20
  simpt_maxk=20
  simpt_bict=0.90

echo $tracebase

${panthera}/install/bin/panthera tracing.calculate_simpoints \
	--simpoint-maxk ${simpt_maxk} \
	--simpoint-bic-threshold ${simpt_bict} \
	--input-json ${tracebase}.generate_bbvs.json 

echo
paste ${tracebase}.calculate_simpoints.weights ${tracebase}.calculate_simpoints.simpoints 

