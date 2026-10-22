# Copyright (c) 2025-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from panthera.cheetah import Cmd, argument_parser, run_study
from panthera.cheetah.studies.bobcat import BobcatSim
from panthera.cheetah.studies.core import CoreSim, trace_list

configurations = [
    {
        "configuration": "baseline",  # name of the configuration (must be unique)
        # Uncomment to add custom amperesys command-lines for this config
        #        'more_core_args': ['-sys.soc_0.core_0.lsu.l1d_cache.ways', '8'],
    },
]

bobcat_cmd = BobcatSim(
    bobcat_args=[
        "-bs", Cmd.join_paths("{workload_root}", "{pan_branch_warmup_file}"),
        "-top.core.decoders", '"[\\"furiosa\\"]"',
        "-top.core:cfg", "{panthera_root}/config/model/core/connor/connor-tage.cfg",
        "-top.core.bpu.save_warmup_file", "{name}.bpu_warmup.json.gz",
    ],
    only_if=Cmd.has("pan_branch_warmup_file"),
)  # fmt: skip
amperesys_cmd = CoreSim(
    core="domino",
    extra_core_args=[
        "{more_core_args}",
        Cmd.if_set(
            "pan_branch_warmup_file",
            ["-sys.soc_0.core_0.bpu.warmup_file", "{name}.bpu_warmup.json.gz"],
        ),
    ],
)
commands = bobcat_cmd + amperesys_cmd

# Build an argument parser from the commands defined above
parser = argument_parser(prog=__file__, description="Run a Core study on a study list")
for command in commands:
    command.parser_add_args(parser)
args = parser.parse_args()

# You could filter the list of traces here if you wanted to limit your study to
# a subset of the study list
traces = trace_list(args.workloads)

# Combine the list of traces with the matrix of configurations
run_study(args.study, commands.apply(traces).apply([vars(args)]).apply(configurations), args)
