# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from panthera.cheetah import Cmd, argument_parser, run_study
from panthera.cheetah.studies.bobcat import BobcatSim
from panthera.cheetah.studies.core import trace_list
from panthera.constants import SUMMARY_CSV_SUFFIX

configurations = [
    {
        "configuration": "baseline",  # name of the configuration (must be unique)
        # Uncomment to add custom bobcat command-lines for this config
        #        'more_bobcat_args': ['-top.core.bpu.tage.btb.ways', '8'],
    },
]

bobcat_cmd = BobcatSim(
    max_instructions = 10000000,
    bobcat_args = [
        "-is", Cmd.join_paths("{workload_root}", "{file}"),
        "-top.core.decoders", '"[\\"furiosa\\"]"',
        "-top.core:cfg", "{panthera_root}/config/model/core/connor/connor-tage.cfg",
        "-top.core.bpu.max_instructions", "{max_instructions}",
        "{more_bobcat_args}",
    ],
    evp = [
        "-i", "*.bpu.instruction_commit=30000",
        "@bobcat.cfg",
        "+summarize", "--summary", f"{{name}}.{SUMMARY_CSV_SUFFIX}",
    ]
)  # fmt: skip

# Build an argument parser from the commands defined above
parser = argument_parser(prog=__file__, description="Run a Bobcat study on a study list's instruction streams")
bobcat_cmd.parser_add_args(parser)
args = parser.parse_args()

# You could filter the list of traces here if you wanted to limit your study to
# a subset of the study list
traces = trace_list(args.workloads)

# Combine the list of traces with the matrix of configurations
run_study(args.study, bobcat_cmd.apply(traces).apply([vars(args)]).apply(configurations), args)
