# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from panthera.cheetah import Cmd, argument_parser, run_study
from panthera.cheetah.studies.core import trace_list
from panthera.cheetah.studies.sim import WorkloadSimCmd
from panthera.constants import SUMMARY_CSV_SUFFIX


class LoopfinderCmd(WorkloadSimCmd):
    loopfinder = "{panthera_root}/bin/is_loopfinder"
    simexe = "{loopfinder_cmd}"
    loopfinder_cmd = [
        "{loopfinder}",
        "-instruction_stream", Cmd.join_paths("{workload_root}", "{file}"),
        "-max_instructions", "{max_instructions}",
        "{loopfinder_args}",
    ]  # fmt: skip
    max_instructions = 10000000
    loopfinder_args = []
    evp = [
        "-i", "1us",
        "+count",
        "-e", "loopfinder.*",
        "+summarize",
        "--summary", f"{{name}}.{SUMMARY_CSV_SUFFIX}",
    ]  # fmt: skip
    directory = "{configuration}"

    def parser_add_args(self, parser, description="Run loopfinder simulation"):
        loopfinder_group = parser.add_argument_group(title="Loopfinder arguments")
        super().parser_add_args(loopfinder_group)


configurations = [
    {
        "configuration": "iq_32",
        "loopfinder_args": [
            "-loopfinder.max_loop_size",
            "32",
        ],
    },
    {
        "configuration": "iq_64",
        "loopfinder_args": [
            "-loopfinder.max_loop_size",
            "64",
        ],
    },
]

loopfinder_cmd = LoopfinderCmd(
    core="domino",
    extra_core_args=[
        "{more_core_args}",
        "-sys.soc_0.core_0.bpu.warmup_file", "{name}.bpu_warmup.json.gz"],
)  # fmt: skip

# Build an argument parser from the commands defined above
parser = argument_parser(prog=__file__, description='Run a "is_loopfinder" study on a study list')
loopfinder_cmd.parser_add_args(parser)
args = parser.parse_args()

# You could filter the list of traces here if you wanted to limit your study to
# a subset of the study list
traces = trace_list(args.workloads)

# Combine the list of traces with the matrix of configurations and launch the
# study on Slurm
run_study(args.study, loopfinder_cmd.apply(traces).apply([vars(args)]).apply(configurations), args)
