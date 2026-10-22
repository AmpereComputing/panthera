# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import random

from panthera.cheetah import argument_parser, run_study
from panthera.cheetah.studies.core import CoreSim, trace_list

DISABLE_L1_PREFETCHERS = [
    "-sys.soc_0.core_0:append-cfg",
    "{panthera_root}/config/model/core/lsu_prefetchers/disable_all.cfg",
]
DISABLE_L2_PREFETCHERS = [
    "-sys.soc_0.core_0.l2c.prefetcher:override-cfg",
    "{panthera_root}/config/model/core/l2c_prefetchers/none.cfg",
]

core = CoreSim(directory="{configuration}", core="domino")

parser = argument_parser(prog=__file__, description="Run a core study with different IDR jitter")

core.parser_add_args(parser)
flakiness = parser.add_argument_group("Flakiness options")
flakiness.add_argument(
    "--amount",
    type=int,
    default=4,
    help="Number of configurations with different jitter",
)
flakiness.add_argument(
    "--no-prefetchers",
    action="store_true",
    help="Also run with no prefetchers enabled",
)
flakiness.add_argument(
    "--max-instructions",
    type=int,
    default=10000000,
    help="Maximum instructions to run",
)
args = parser.parse_args()

random.seed(0)
seeds = [random.randint(1, 1024) for x in range(args.amount)]


def jittered(name, params=None):
    params = params or []
    configs = [{"configuration": name, "extra_core_args": params}]
    for i, seed in enumerate(seeds):
        configs.append(
            {
                "configuration": f"{name}_jitter_{i}",
                "extra_core_args": params
                + ["-sys.soc_0.core_0.idr.random_jitter", str(seed)],
            }
        )  # fmt: skip
    return configs


# Run configurations with different IDR jitter seeds
configurations = []
configurations.extend(jittered("base"))
if args.no_prefetchers:
    configurations.extend(jittered("no_prefetch", DISABLE_L1_PREFETCHERS + DISABLE_L2_PREFETCHERS))

traces = trace_list(args.workloads)

run_study(args.study, core.apply([vars(args)]).apply(traces).apply(configurations), args)
