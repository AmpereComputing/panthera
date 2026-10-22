# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from panthera.cheetah import argument_parser, run_study
from panthera.cheetah.studies.core import trace_list
from panthera.cheetah.studies.sim import ArbitraryCmd

# Build an argument parser from the commands defined above
parser = argument_parser(prog=__file__, description="Run panthera-oriented command")
command = ArbitraryCmd()
command.parser_add_args(parser)
args = parser.parse_args()

# You could filter the list of traces here if you wanted to limit your study to
# a subset of the study list
traces = trace_list(args.workloads)

run_study(args.study, command.apply(traces).apply([vars(args)]), args)
