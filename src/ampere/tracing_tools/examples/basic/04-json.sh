#!/bin/bash
# Copyright (c) 2022 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


  panthera=../panthera
  basename=basic_0 

${panthera}/install/bin/panthera events.tracing ${basename}.es \
            @${panthera}/install/config/events.tracing/summary.cfg
