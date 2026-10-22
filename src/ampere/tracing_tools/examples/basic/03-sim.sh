#!/bin/bash
# Copyright (c) 2022 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


  panthera=../panthera
  basename=basic_0

${panthera}/install/bin/panthera sim ${panthera}/install/bin/amperesys \
   -cfg ${panthera}/install/config/model/sys/siryn.cfg                 \
   -sys.soc_0.core_0.instruction_stream_name ${basename}.is.xz         \
   -sys.soc_0.core_0.gpc.max_instructions 10000                        \
   -- -i 10us @base.cfg +capture ${basename}.es +summarize --timeseries ${basename}.ts.csv --summary ${basename}.summary.csv


