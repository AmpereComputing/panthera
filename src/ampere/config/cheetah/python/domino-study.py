# Copyright (c) 2025-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from panthera.cheetah import CmdPool, argument_parser, run_study
from panthera.cheetah.studies.soc import (
    SoCSim,
    SoCValidation,
    bb_cmd,
    bw_cmd,
    c2c_bw_cmd,
    c2c_lat_cmd,
    c2c_ld_lat_cmd,
    ior_bw_cmd,
    ior_lat_cmd,
    ior_ld_lat_cmd,
    ior_rwbw_cmd,
    lat_cmd,
    mixed_ior_bw_cmd,
    multi_ior_bw_cmd,
    multicore_cmd,
    multiload_cmd,
)

ALL_PROGS = "0r1w,1r0w,1r1w,1r2w,1r3w,1r4w,1r5w,1r6w,2r1w,3r1w,4r1w,5r1w,6r1w"
DEFAULT_PROGS = "0r1w,1r0w,1r1w,2r1w,3r1w"
ALL_INTERVALS = "2048,1024,512,400,256,200,128,100,80,64,50,40,32,20,16,12,10,8,6,4,2,1,0"
DEFAULT_INTERVALS = "2048,128,64,40,32,20,16,8,4,0" + ",42,44,46,48"
IOR_PROGS = "reads,writes,mixed"
MEM_SIZES = ["16384k", "24576k", "30720k", "32768k", "34816k", "36864k", "40960k"]


def prog_list(arg):
    return [{"prog": p} for p in arg.split(",")]


def int_list(arg):
    return [int(x) for x in arg.split(",")]


def str_list(arg):
    return arg.split(",")


parser = argument_parser(description="Run a Domino study")
SoCSim().parser_add_args(parser)
domino_group = parser.add_argument_group(title="Domino Study arguments")
domino_group.add_argument("--all", action="store_true", help="Run all kinds of workload types")
domino_group.add_argument(
    "--topology", action="append", choices=["octant", "quad", "hemi"], default=[], help="Topology Configuration"
)
domino_group.add_argument("--dashboard", action="store_true", help="Run workloads for the dashboard")
domino_group.add_argument("--latency", action="store_true", default=False, help="Enable latency tests")
domino_group.add_argument("--lat-mem-rd", action="store_true", default=False, help="Enable lat_mem_rd test")
domino_group.add_argument("--bandwidth", action="store_true", default=False, help="Enable bandwidth tests")
domino_group.add_argument("--multiload", action="store_true", default=False, help="Enable multiload tests")
domino_group.add_argument(
    "--multiload-cores",
    default="1,2,4,8,12,16,20,24,28,32,36,40,42",
    type=int_list,
    help="Aggressor cores for multiload",
)
domino_group.add_argument("--bb", action="store_true", default=False, help="Enable badger bench tests")
domino_group.add_argument("--ior-lat", action="store_true", default=False, help="Enable IOR idle latency tests")
domino_group.add_argument("--ior-ld-lat", action="store_true", default=False, help="Enable IOR loaded latency tests")
domino_group.add_argument("--ior-bw", action="store_true", default=False, help="Enable IOR BW tests")
domino_group.add_argument("--ior-rwbw", action="store_true", default=False, help="Enable IOR RWBW tests")
domino_group.add_argument(
    "--eq-bw", action="store_true", default=False, help="Set repeat of Reads or Writes to match bandwidth"
)
domino_group.add_argument("--mixed-ior-bw", action="store_true", default=False, help="Enable IOR Mixed BW tests")
domino_group.add_argument("--multi-ior-bw", action="store_true", default=False, help="Enable IOR BW tests")
domino_group.add_argument("--multi-iors", default="0,2,4,6", type=int_list, help="Active IORs for multi ior runs")
domino_group.add_argument(
    "--multi-iors-prog",
    default="reads,reads,writes,writes",
    type=str_list,
    help="Programs for IORs. Should be of the same length as the number of IORs",
)
domino_group.add_argument(
    "--prog", type=prog_list, default=prog_list(DEFAULT_PROGS), help="Program(s) for load generating (aggressor) cores"
)
domino_group.add_argument(
    "--prog-latency-core", type=str, default="chase", help="Program(s) for load latency core in badger bench runs"
)
domino_group.add_argument("--c2c_latency", action="store_true", default=False, help="Enable core to core latency tests")
domino_group.add_argument(
    "--c2c_ld_latency", action="store_true", default=False, help="Enable core to core loaded latency tests"
)
domino_group.add_argument("--c2c_bw", action="store_true", default=False, help="Enable core to core bw tests")
domino_group.add_argument("--multicore", action="store_true", default=False, help="Enable multicore tests")
domino_group.add_argument(
    "--cbusy-disable", dest="cbusy_enable", action="store_false", default=True, help="Disables cbusy"
)
domino_group.add_argument(
    "--mem-interval", type=int_list, default=int_list(DEFAULT_INTERVALS), help="Repeat counts for inter-memory uops"
)
domino_group.add_argument(
    "--print-only", action="store_true", default=False, help="Don't run a study, just print the commands"
)
domino_group.add_argument("--skip-validation", action="store_true", default=False, help="Don't validation results")
args = parser.parse_args()

if not args.topology:
    args.topology = ["octant"]

core_count = {"octant": (64), "quad": (64 * 2), "hemi": (64 * 4)}

soc_cfg = {"octant": "dn-octant.cfg", "quad": "dn-quad1.cfg", "hemi": "dn-hemi0.cfg"}
max_iors = 8


def latency_cmds():
    """Measure idle latency.

    Dimensions:
        1. "All" core positions (actually one position per CCM)
        2. DDR and L3 using different memory buffer sizes.
    """
    pool = CmdPool()
    for topology in args.topology:
        cores_per_ccm = 4
        core_list = range(0, core_count[topology], cores_per_ccm)

        active_cores = [{"active_core": n} for n in core_list]

        sizes = [{"mem_size": "128M"}, {"mem_size": "8M", "enable_warmup": "true", "high_stash_effort": "true"}]
        cmds = lat_cmd

        # if not args.skip_validation:
        #    cmds += SoCValidation()

        pool = pool.concat(cmds.apply(active_cores).apply(sizes).apply([{"soc_cfg": soc_cfg[topology]}]))

    return pool


def lat_mem_rd_cmds():
    pool = CmdPool()
    for topology in args.topology:
        active_core = 0
        mem_size = [{"mem_size": m} for m in MEM_SIZES]
        cmd = lat_cmd
        cmd.sim += [f"-sys.soc_0.core_{active_core}.source.loop_count", 2]
        cmd.evp = [f"--start", f"sys.soc_0.core_{active_core}.source.loop=1"] + cmd.evp
        cmd.evp = [f"--stop", f"sys.soc_0.core_{active_core}.source.loop=2"] + cmd.evp
        cmd.evp = ["--exit"] + cmd.evp
        pool = pool.concat(cmd.apply(mem_size).apply([{"max_time": "1s"}]))

    return pool


def bandwidth_cmds():
    """Measure single-core bandwidth.

    Dimensions:
        1. "All" core positions (actually one position per CCM)
        2. DDR and L3 using different memory buffer sizes.
        3. Program list (1r0w, 0w1r, ...)
    """
    pool = CmdPool()
    for topology in args.topology:
        cores_per_ccm = 4
        core_list = range(0, core_count[topology], cores_per_ccm)

        active_cores = [{"active_core": n} for n in core_list]

        sizes = [
            {"mem_size": "128M"},  # DRAM sized
            {"mem_size": "8M", "enable_warmup": "true", "high_stash_effort": "true"},  # L3 sized
        ]
        pool = pool.concat(
            bw_cmd.apply(active_cores).apply(sizes).apply([{"soc_cfg": soc_cfg[topology]}]).apply(args.prog)
        )

    return pool


def multiload_cmds():
    """Mimic Google's multiload benchmark.

    Dimensions:
        1. Set of aggressor cores (this set grows to drive bandwidth up)
        2. Aggressor program list

    We do not currently vary the latency core, which is running a pointer chasing
    program. This is hard-coded to core 0 for now.
    """
    multiload_cmds = CmdPool()
    for ncores in args.multiload_cores:
        nagg = ncores - 1
        cmd = multiload_cmd.copy(nagg=nagg, latency_core=0)
        num = max_cores
        den = max_cores
        for i in range(0, max_cores):
            if num >= den:
                if domino_config == True:
                    stash_only = "false"
                    enable = "true"
                else:
                    stash_only = "false"
                    enable = "true"
                num -= den
            else:
                if domino_config == True:
                    stash_only = "true"
                    enable = "true"
                else:
                    stash_only = "false"
                    enable = "false"
            num += ncores
            cmd.sim += [
                f"-sys.soc_0.core_{i}.enable",
                enable,
                f"-sys.soc_0.core_{i}.stash_only",
                stash_only,
            ]
        multiload_cmds = multiload_cmds.concat(cmd.apply(args.prog))
    return multiload_cmds


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
    for topology in args.topology:
        for interval in args.mem_interval:
            if interval >= 1024:
                warm_time = 1000
                run_time = warm_time + 500
            elif interval >= 128:
                warm_time = 500
                run_time = warm_time + 200
            else:
                warm_time = 100
                run_time = warm_time + 100

            cmds = bb_cmd.copy(
                interval=interval,
                latency_core=0,
                enable_cbusy=args.cbusy_enable,
                prog_latency_core=args.prog_latency_core,
                enable_warmup="true",
                warmup_time=f"{warm_time}us",
                max_time=f"{run_time}us",
            )

            if not args.skip_validation:
                cmds += SoCValidation(
                    verify_cmds="pxha_eq_distributed pmha_eq_distributed stash_reject_rate retry_rate cleaninvalid_rate"
                )

            pool = pool.concat(cmds.apply([{"soc_cfg": soc_cfg[topology]}]).apply(args.prog))
    return pool


def multicore_cmds():
    """Have multiple cores read a shared memory region.

    This is currently broken because we've changed the way shared memory works.
    """
    num_sharers = 1
    cmd = multicore_cmd.copy(prog="1r0w")
    for i in range(0, 64):
        cmd.sim += [f"-sys.soc_0.core_{i}.data_offset", f"0x{0x1000001000 * (i // num_sharers):x}"]
    for i in range(64, max_cores):
        cmd.sim += [f"-sys.soc_0.core_{i}.stash_only", "true"]
    return cmd


def c2c_latency_cmds():
    """Measure core to core transfer latency."""
    c2c_cmds = CmdPool()
    mask = 0
    if domino_config is True:
        address_list = f"[133056,134080,135104,136128,137152,138176,139200,140224,131072,131136,131200,131264,131328,131392,131456,131520,131584,131648,131712,131776,131840,131904,131968,132032]"
    else:
        address_list = f"[131200,131264,131712,131776,132224,132288,132736,132800,133184,133120,133696,133632,134208,134144,134720,134656,131456,131520,134080,139968,134592,134528,132992,133056,133440,133376,133952,133888,134464,134400,134976,134912]"
    for rows in range(max_cores - 1):
        for cols in range(rows, max_cores - 1):
            mask = 1 << (rows % 64)
            mask |= 1 << ((cols + 1) % 64)
            cmd = c2c_lat_cmd.copy(mask=mask, core1=rows, core2=(cols + 1), address_list=address_list)
            cmd.sim += [f"-sys.soc_0.core_{rows}.stash_only", "false"]
            cmd.sim += [f"-sys.soc_0.core_{cols + 1}.stash_only", "false"]
            c2c_cmds = c2c_cmds.concat(cmd.apply([{"enable_events_on_start": "false"}]))
    return c2c_cmds


def c2c_loaded_latency_cmds():
    """Measure core to core transfer latency under load."""
    c2c_cmds = CmdPool()
    mask = 0
    if domino_config is True:
        address_list = f"[133056,134080,135104,136128,137152,138176,139200,140224,131072,131136,131200,131264,131328,131392,131456,131520,131584,131648,131712,131776,131840,131904,131968,132032]"
    else:
        address_list = f"[131200,131264,131712,131776,132224,132288,132736,132800,133184,133120,133696,133632,134208,134144,134720,134656,131456,131520,134080,139968,134592,134528,132992,133056,133440,133376,133952,133888,134464,134400,134976,134912]"
    for interval in args.mem_interval:
        for rows in range(max_cores - 1):
            for cols in range(rows, max_cores - 1):
                mask = 1 << (rows % 64)
                mask |= 1 << ((cols + 1) % 64)
                cmd = c2c_ld_lat_cmd.copy(
                    mask=mask, core1=rows, core2=(cols + 1), address_list=address_list, interval=interval
                )
                for i in range(0, max_cores):
                    cmd.sim += [f"-sys.soc_0.core_{i}.data_offset", f"0x{0x1000001000 * (i + 1):x}"]
                cmd.sim += [f"-sys.soc_0.core_{rows}.data_offset", f"0x0"]
                cmd.sim += [f"-sys.soc_0.core_{cols + 1}.data_offset", f"0x0"]
                c2c_cmds = c2c_cmds.concat(cmd.apply([{"enable_events_on_start": "false"}]))
    return c2c_cmds


def c2c_bandwidth_cmds():
    """Measure core to core transfer bandwidth.

    This is done by moving a memory region among different core's L2 caches.
    """
    c2c_cmds = CmdPool()
    mask = 0
    for rows in range((max_cores // 2) - 1):
        for cols in range(rows, (max_cores // 2) - 1):
            mask = 1 << rows
            mask |= 1 << (cols + 1)
            cmd = c2c_bw_cmd.copy(mask=mask, core1=rows, core2=(cols + 1), mem_size="2M")
            cmd.sim += [f"-sys.soc_0.core_{rows}.stash_only", "false"]
            cmd.sim += [f"-sys.soc_0.core_{cols + 1}.stash_only", "false"]
            c2c_cmds = c2c_cmds.concat(cmd.apply([{"enable_events_on_start": "true"}]))
    return c2c_cmds


def ior_latency_cmds():
    active_iors = [{"active_ior": n} for n in range(0, max_iors)]
    return ior_lat_cmd.apply(active_iors)


def ior_ld_latency_cmds():
    ior_ld_lat_cmds = CmdPool()
    active_iors = [{"active_ior": n} for n in range(0, max_iors)]
    for interval in args.mem_interval:
        cmd = ior_ld_lat_cmd.copy(interval=interval)
        ior_ld_lat_cmds = ior_ld_lat_cmds.concat(cmd.apply(active_iors))
    return ior_ld_lat_cmds


# In this test, one source of IOR is doing pure Reads/Writes test
def ior_bw_cmds():
    ior_cmds = CmdPool()
    active_iors = [{"active_ior": n} for n in range(0, max_iors)]

    cmd = ior_bw_cmd.copy()

    sizes = [
        {"mem_size": "128M", "prog": "reads"},  # DRAM sized reads
        {"mem_size": "32M", "enable_warmup": "true", "high_stash_effort": "true", "prog": "reads"},  # L3 sized reads
        {"mem_size": "128M", "prog": "writes"},  # DRAM sized writes
        {
            "mem_size": "32M",
            "warmup_size": "32M",
            "enable_warmup": "true",
            "high_stash_effort": "true",
            "alloc": "true",
            "prog": "writes",
        },  # L3 sized writes, stash in L3 with L3 hit
    ]

    ior_cmds = ior_cmds.concat(cmd.apply(active_iors).apply([{"soc_cfg": "dn-io-octant.cfg"}]).apply(sizes))

    return ior_cmds


# In this test, one source if IOR is doing mix of Reads and Writes of different TLP sizes
def ior_rwbw_cmds():
    ior_cmds = CmdPool()
    active_iors = [{"active_ior": n} for n in range(0, max_iors)]
    read_sizes = [4096, 2048, 1024, 512, 256, 128]
    write_sizes = [512, 256, 128]

    axi_latencies = [20]
    # iot_sizes = [2, 4, 8, 16, 32, 40, 64, 80, 128, 160, 190, 210, 240, 256, 320, 512]
    iot_sizes = [512]
    latency_sweep = [{"delay": n} for n in axi_latencies]
    iot_size = [{"iot_size": n} for n in iot_sizes]

    configs = []

    for r in read_sizes:
        for w in write_sizes:
            wrepeat = (r // w if r > w else 1) if args.eq_bw else 1
            rrepeat = (w // r if w > r else 1) if args.eq_bw else 1
            config = {"RTLP_size": r, "WTLP_size": w, "prog": "mixed", "wrepeat": wrepeat, "rrepeat": rrepeat}
            configs.append(config)

    cmd = ior_rwbw_cmd.copy()

    ior_cmds = ior_cmds.concat(cmd.apply(active_iors).apply(configs).apply(latency_sweep).apply(iot_size))

    return ior_cmds


# In this study, 4 sources of IOR are active and 2 sources are doing reads and the other two are doing writes
def mixed_ior_bw_cmds():
    ior_cmds = CmdPool()
    active_iors = [{"active_ior": n} for n in range(0, max_iors)]
    read_sizes = [{"RTLP_size": "512"}]
    write_sizes = [{"WTLP_size": "512"}]

    tracker_sizes = [
        {"nrt_size": "580", "prt_size": "580"},
        {"nrt_size": "512", "prt_size": "512"},
        {"nrt_size": "480", "prt_size": "480"},
        {"nrt_size": "420", "prt_size": "420"},
        {"nrt_size": "360", "prt_size": "360"},
        {"nrt_size": "320", "prt_size": "320"},
        {"nrt_size": "280", "prt_size": "280"},
        {"nrt_size": "220", "prt_size": "220"},
        {"nrt_size": "180", "prt_size": "180"},
        {"nrt_size": "120", "prt_size": "120"},
        {"nrt_size": "80", "prt_size": "80"},
    ]
    cmd = mixed_ior_bw_cmd.copy()
    ior_cmds = ior_cmds.concat(cmd.apply(active_iors).apply(read_sizes).apply(write_sizes).apply(tracker_sizes))

    return ior_cmds


def multi_ior_bw_cmds():
    ior_cmds = CmdPool()
    cmd = multi_ior_bw_cmd.copy()

    sizes = [
        # {'TLP_size': '4096'},
        # {'TLP_size': '2048'},
        # {'TLP_size': '1024'},
        {"TLP_size": "512"}
        # {'TLP_size': '256'},
        # {'TLP_size': '128'}
    ]

    multi_iors = [
        # Skip pairs
        {0: "reads", 2: "reads"},
        {0: "reads", 2: "writes"},
        {0: "writes", 2: "writes"},
        {0: "reads", 6: "reads"},
        {0: "reads", 6: "writes"},
        {0: "writes", 6: "writes"},
        {4: "reads", 6: "reads"},
        {4: "reads", 6: "writes"},
        {4: "writes", 6: "writes"},
        # Adjacent pairs
        {2: "reads", 3: "writes"},
        {2: "reads", 3: "reads"},
        {2: "writes", 3: "writes"},
        {6: "reads", 7: "writes"},
        {6: "reads", 7: "reads"},
        {6: "writes", 7: "writes"},
        {0: "reads", 1: "writes"},
        {0: "reads", 1: "reads"},
        {0: "writes", 1: "writes"},
        {4: "reads", 5: "writes"},
        {4: "reads", 5: "reads"},
        {4: "writes", 5: "writes"},
        {3: "reads", 7: "writes"},
        {3: "reads", 7: "reads"},
        {3: "writes", 7: "writes"},
        {0: "reads", 4: "reads"},
        {0: "reads", 4: "writes"},
        {0: "writes", 4: "writes"},
    ]

    for entries in multi_iors:
        iors = ""
        for ior, prog in zip(entries.keys(), entries.values()):
            cmd.sim += [f"-sys.soc_0.iorinjector_{ior}.enable", "true"]
            cmd.sim += [f"-sys.soc_0.iorinjector_{ior}.source_0:cfg", f"{{config_model}}/inj/prog_ior_{prog}.cfg"]
            p = "r" if prog == "reads" else "w"
            iors += str(ior) + p
        ior_c = [{"iors": iors}]
        ior_cmds = ior_cmds.concat(cmd.apply(sizes).apply(ior_c))
        cmd = multi_ior_bw_cmd.copy()
    return ior_cmds


cmds = CmdPool()


if args.dashboard:
    args.topology = ["octant", "quad", "hemi"]
    args.latency = True
    args.bandwidth = True
    args.bb = True

if args.latency or args.all:
    cmds = cmds.concat(latency_cmds())

if args.lat_mem_rd:
    cmds = cmds.concat(lat_mem_rd_cmds())

if args.bandwidth or args.all:
    cmds = cmds.concat(bandwidth_cmds())

if args.multiload or args.all:
    cmds = cmds.concat(multiload_cmds())

if args.bb or args.all:
    cmds = cmds.concat(badgerbench_cmds())

if args.ior_lat:
    cmds = cmds.concat(ior_latency_cmds())

if args.ior_ld_lat:
    cmds = cmds.concat(ior_ld_latency_cmds())

if args.ior_bw:
    cmds = cmds.concat(ior_bw_cmds())

if args.ior_rwbw:
    cmds = cmds.concat(ior_rwbw_cmds())

if args.mixed_ior_bw:
    cmds = cmds.concat(mixed_ior_bw_cmds())

if args.multi_ior_bw:
    cmds = cmds.concat(multi_ior_bw_cmds())

if args.c2c_latency or args.all:
    cmds = cmds.concat(c2c_latency_cmds())

if args.c2c_ld_latency or args.all:
    cmds = cmds.concat(c2c_loaded_latency_cmds())

if args.c2c_bw or args.all:
    cmds = cmds.concat(c2c_bandwidth_cmds())

overrides = {}

if args.panthera_root:
    overrides["panthera_root"] = args.panthera_root

if args.soc_cfg:
    overrides["soc_cfg"] = args.soc_cfg

if overrides:
    cmds = cmds.apply([overrides])

if args.print_only:
    args.run = "printer"
else:
    args.slurm_default_mem = 8192

run_study("domino", cmds, args)
