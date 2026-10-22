# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import re

from panthera.cheetah import CmdPool, argument_parser, run_study
from panthera.cheetah.studies.soc import SoCSim, SoCValidation, lat_cmd

DEFAULT_PROGS = "0r1w,1r0w,1r1w,2r1w,3r1w"
ALL_INTERVALS = "2048,1024,512,400,256,200,128,100,80,64,50,40,32,20,16,12,10,8,6,4,2,1,0"
DEFAULT_INTERVALS = "2048,128,64,48,46,44,42,40,38,36,34,32,20,16,8,4,0"

SIZE_RE = re.compile("(\d+)(k|M|G)?")
SUFFIX_MUL = {"k": 1024, "M": 1024 * 1024, "G": 1024 * 1024 * 1024}


class AddressRegions:
    def __init__(self):
        pass

    def __call__(self, cmd):
        params = []
        for core in range(1, 64):
            banger_mode = 1
            start_addr = (core << (24 + banger_mode + 5)) + (0x375040 * core)
            stop_addr = start_addr + AddressRegions.calc_mem_size(cmd)
            warmup_size = 1024 * 1024 * 4
            warmup_start_addr = stop_addr - warmup_size

            params += [
                f"-sys.soc_0.core_{core}.source.start_addr", f"{hex(start_addr)}",
                f"-sys.soc_0.core_{core}.source.stop_addr", f"{hex(stop_addr)}",
                f"-sys.soc_0.core_{core}.source.warmup_start_addr", f"{hex(warmup_start_addr)}",
                f"-sys.soc_0.core_{core}.source.warmup_stop_addr", f"{hex(stop_addr)}",
            ]  # fmt: skip
        return params

    @staticmethod
    def calc_mem_size(cmd):
        mem_size = cmd.render("{mem_size}")
        m = SIZE_RE.match(mem_size)
        if m:
            number, suffix = m.groups([1, 2])
            mem_size = int(number) * SUFFIX_MUL[suffix]
        return mem_size


bb_cmd = SoCSim(
    experiment      = "bb",
    name            = "bb_lat{latency_core}_{interval}_{prog}_{mem_size}",
    soc_cfg         = "dn-octant-rtl.cfg",
    latency_core    = 0,
    prog            = "1r0w",
    interval        = "interval",
    mem_size        = "128M",
    max_time        = "2ms",
    warmup_time     = "0ms",
    address_regions = AddressRegions(),
    enable_warmup   = "false",
    enable_cbusy    = "false",
    bank_hash_bits  = 0,
    sim             = SoCSim.sim + [
        r"-~sys\.soc_0\.core_[0-9]+\.enable", "true",
        r"-~sys\.soc_0\.core_[0-9]+\.source:cfg", "{config_model}/inj/prog_{prog}.cfg",
        r"-~sys\.soc_0\.core_[0-9]+\.source.interval", "{interval}",
        r"-~sys\.soc_0\.core_[0-9]+\.source.loop_count", "400000",
        r"-~sys\.soc_0\.core_[0-9]+\.source\.enable_events_on_start", "true",
        r"-~sys\.soc_0\.core_[0-9]+\.source\.enable_warmup", "true",
        r"-~sys\.soc_0\.core_[0-9]+\.brain_dead_limit", "1000000000",
        r"-sys.soc_0.core_{latency_core}.source.loop_count", "49",
        "-sys.soc_0.core_{latency_core}.source:cfg", "{config_model}/inj/prog_chase_linear.cfg",
        "-sys.soc_0.core_{latency_core}.source.enable_warmup", "{enable_warmup}",
        r"-~sys\.soc_0\.core_[0-9]+\.cbusy\.enable", "{enable_cbusy}",
        r"-~sys\.soc_0\.core_[0-9]+\.bank_hash_bits", "{bank_hash_bits}",
        r"-~sys\.soc_0\.pxha_[0-9]+\.cbusy\.enable", "{enable_cbusy}",
        r"-~sys\.soc_0\.pmha_[0-9]+\.cbusy\.enable", "{enable_cbusy}",
        "{address_regions}"
    ],
    evp_cfgs      = ["@lat_bw_inj.cfg", "@idt.cfg", "@mesh-congestion.cfg"],
    evp_plugins   = ["+python", "inj_req_to_req_lat.py", "--latency-core", "{latency_core}",
                     "+python", "lat_breakdown.py", "--latency-core", "{latency_core}", "+capture", "events.es.xz"],
    directory     = "cfg.{short_cfg}/experiment.{experiment}/latency_core.{latency_core}/interval.{interval}/workload.{prog}/mem_size.{mem_size}"
)  # fmt: skip


def prog_list(arg):
    return [{"prog": p} for p in arg.split(",")]


def int_list(arg):
    return [int(x) for x in arg.split(",")]


parser = argument_parser(description="Run a Domino study")
SoCSim().parser_add_args(parser)
domino_group = parser.add_argument_group(title="Domino Study arguments")
domino_group.add_argument("--all", action="store_true", help="Run all kinds of workload types")
domino_group.add_argument("--latency", action="store_true", default=False, help="Enable latency tests")
domino_group.add_argument("--bb", action="store_true", default=False, help="Enable badger bench tests")
domino_group.add_argument(
    "--prog", type=prog_list, default=prog_list(DEFAULT_PROGS), help="Program(s) for load generating (aggressor) cores"
)
domino_group.add_argument(
    "--mem-interval", type=int_list, default=int_list(DEFAULT_INTERVALS), help="Repeat counts for inter-memory uops"
)
domino_group.add_argument(
    "--print-only", action="store_true", default=False, help="Don't run a study, just print the commands"
)
domino_group.add_argument("-o", "--output", default="domino_pv", help="Output directory")
args = parser.parse_args()


def latency_cmds():
    """Measure idle latency.

    Dimensions:
        1. "All" core positions (actually one position per CCM)
        2. DDR and L3 using different memory buffer sizes.
    """
    pool = CmdPool()
    cores_per_ccm = 4
    core_list = range(0, 64, cores_per_ccm)

    active_cores = [{"active_core": n} for n in core_list]

    sizes = [{"mem_size": "128M"}, {"mem_size": "8M", "enable_warmup": "true", "high_stash_effort": "true"}]
    cmds = lat_cmd
    pool = pool.concat(
        cmds.apply(active_cores)
        .apply(sizes)
        .apply([{"soc_cfg": "dn-octant-rtl.cfg"}])
        .apply(
            [
                {
                    "evp_plugins": [
                        "+capture",
                        "events.es.xz",
                        "+python",
                        "soc_print_path_latencies.py",
                        "--start-transaction",
                        "ReadShared",
                        "+python",
                        "lat_breakdown.py",
                        "--latency-core",
                        "{active_core}",
                    ]
                }
            ]
        )
    )

    return pool


def badgerbench_cmds():
    """Mimic BadgerBench.

    This is an alternative similar to multiload that ramps up bandwidth in every core
    at once by reducing the idle interval between memory operations.

    Dimensions:
        1. Inter-memory interval
        2. Aggressor program list

    Because a larger interval implies fewer memory transactions per unit time, it is
    necessary to increase the warmup time for larger intervals. The numbers here were
    determined empirically and will require some maintenance (unless we enhance the
    warmup mechanism to go by transaction count or L2 miss rate or something instead
    of just time).

    As for multiload, the latency core is always core 0.
    """
    pool = CmdPool()
    for interval in args.mem_interval:
        cmds = bb_cmd.copy(interval=interval)
        cmds += SoCValidation(
            verify_cmds="pxha_eq_distributed pmha_eq_distributed stash_reject_rate retry_rate cleaninvalid_rate"
        )
        pool = pool.concat(cmds.apply([{"soc_cfg": "dn-octant-rtl.cfg"}]).apply(args.prog))
    return pool


cmds = CmdPool()


if args.latency or args.all:
    cmds = cmds.concat(latency_cmds())

if args.bb or args.all:
    cmds = cmds.concat(badgerbench_cmds())

overrides = {}

if args.panthera_root:
    overrides["panthera_root"] = args.panthera_root

if overrides:
    cmds = cmds.apply([overrides])

if args.print_only:
    args.run = "printer"
else:
    args.slurm_default_mem = 8192

run_study(args.output, cmds, args)
