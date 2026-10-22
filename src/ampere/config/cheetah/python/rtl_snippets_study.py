# Copyright (c) 2025-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import sys

from panthera.cheetah import Cmd, argument_parser, run_study
from panthera.cheetah.studies.bobcat import BobcatSim
from panthera.cheetah.studies.core import CoreRTLSim, trace_list
from panthera.cheetah.studies.evp import EVPCmd
from panthera.constants import SUMMARY_CSV_SUFFIX, TIMESERIES_CSV_SUFFIX

soc_core_mapping = {
    "nebula": "alita",
    "kestrel": "connor-ai",
    "domino": "connor",
    "rogue": "trinity",
    "banshee": "ripley",
    "siryn": "furisoa",
}

configurations = [
    {
        "configuration": "panthera",
        "max_instructions": "{end_instructions}",
        "more_core_args": [
            "-sys.soc_0.core_0:append-cfg",
            "{panthera_root}/config/model/core/pv/base.cfg",
        ],
    },
]

evp_config = [
    "-i", "{interval_trigger}",
    "{evp_cfg_file}",
    "+summarize",
    "--timeseries", f"{{name}}.all.{TIMESERIES_CSV_SUFFIX}",
    "--summary", f"{{name}}.all.{SUMMARY_CSV_SUFFIX}",
    "+capture", "{name}.es.xz",
]  # fmt: skip
bobcat_cmd = BobcatSim(
    bobcat_args=[
        "-bs", Cmd.join_paths("{workload_root}", "{pan_branch_warmup_file}"),
        "-top.core.decoders", '"[\\"furiosa\\"]"',
        "-top.core:cfg", "{panthera_root}/config/model/core/{core_name}/{core_name}-tage.cfg",
        "-top.core.bpu.save_warmup_file", "{name}.bpu_warmup.json.gz",
    ],
    only_if=Cmd.has("pan_branch_warmup_file"),
)  # fmt: skip
amperesys_cmd = CoreRTLSim(
    core="{soc}-core-rtl",
    evp=evp_config,
    evp_cfg_file="perfval.cfg",
    extra_core_args=[
        Cmd.if_set(
            ["bpu_warmstart", "pan_branch_warmup_file"],
            ["-sys.soc_0.core_0.bpu.warmup_file", "{name}.bpu_warmup.json.gz"],
        ),
        "-event_stream_always", "true",
        "-sys.soc_0.core_0.gpc.warmup_instructions", "0",
        "{more_core_args}",
        "{additional_core_args}",
    ],
)  # fmt: skip


class pick_warmup:
    def __init__(self, end: str, warmups: list[str], default_value: int = 0):
        self.end_name = end
        self.warmup_names = warmups
        self.default_value = default_value

    def __call__(self, cmd):
        end_value = Cmd.as_numeric(cmd.render(getattr(cmd, self.end_name)))
        warmup_values = [Cmd.as_numeric(cmd.render(getattr(cmd, v))) for v in self.warmup_names if getattr(cmd, v)]
        for value in warmup_values:
            if (end_value - value) > 1:
                return value

        return self.default_value


evp_cmd = EVPCmd(
    directory="{configuration}",
    evp_end_instructions=Cmd.min_value("end_instructions", "measurement_instructions"),
    evp_warmup_instructions=pick_warmup("evp_end_instructions", ["explicit_warmup_instructions", "warmup_instructions"]),
    start="sys.soc_0.core_0.gpc.instruction_commit={evp_warmup_instructions}",
    stop="sys.soc_0.core_0.gpc.instruction_commit={evp_end_instructions}",
    config="perfval.cfg",
    args=[
        "+summarize",
        "--timeseries", f"{{name}}.{TIMESERIES_CSV_SUFFIX}",
        "--summary", f"{{name}}.{SUMMARY_CSV_SUFFIX}",
    ],
)  # fmt: skip

# Build an argument parser from the commands defined above
parser = argument_parser(prog=__file__, description="Run RTL snippets for PV")
snippets = parser.add_argument_group("Snippets")
snippets.add_argument("--soc", default="domino", choices=list(soc_core_mapping.keys()), help="SoC to run")
snippets.add_argument("--bpu-warmstart", action="store_true", default=False, help="Enable BPU Warm-start")
snippets.add_argument("--no-l2-warmup", action="store_true", help="Disable L2 warmup")
snippets.add_argument(
    "--warmup-instructions",
    type=int,
    default=10000,
    dest="explicit_warmup_instructions",
    help="The number of warmup instructions",
)
snippets.add_argument(
    "--end-instructions",
    type=int,
    default=30000,
    help="The number of instructions to run (including warmup)",
)
snippets.add_argument("additional_core_args", nargs="*")

# Do an initial parsing of the top-level arguments to figure out whether we're
# doing BPU warm-start. If we are, add the arguments for the right set of
# commands to the parser
initial_args, _ = parser.parse_known_args([arg for arg in sys.argv[1:] if arg not in ("-h", "--help")])
if initial_args.bpu_warmstart:
    commands = bobcat_cmd + amperesys_cmd + evp_cmd
else:
    commands = amperesys_cmd + evp_cmd

# Add the right set of commands to the parser, then parse the arguments again
for command in commands:
    command.parser_add_args(parser)
args = parser.parse_args()

if args.explicit_warmup_instructions >= args.end_instructions:
    parser.error("--warmup-instructions should be less than --end-instructions")

# You could filter the list of traces here if you wanted to limit your study to
# a subset of the study list
traces = trace_list(args.workloads)

# Remove preload file to disable L2 warmup
if args.no_l2_warmup:
    for trace in traces:
        del trace["pan_cache_preload_file"]

variables = vars(args)
variables["core_name"] = soc_core_mapping[variables["soc"]]

# Combine the list of traces with the matrix of configurations and launch the
# study on Slurm
run_study(args.study, commands.apply(traces).apply([vars(args)]).apply(configurations), args)
