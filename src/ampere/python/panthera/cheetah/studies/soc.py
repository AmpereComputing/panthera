# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import re
from argparse import ArgumentParser

from panthera.constants import SUMMARY_CSV_SUFFIX, TIMESERIES_CSV_SUFFIX

from ..cmd import Cmd
from .panthera import PantheraCmd
from .sim import SimCmd

SIZE_RE = re.compile(r"(\d+)(k|M|G)?")
SUFFIX_MUL = {"k": 1024, "M": 1024 * 1024, "G": 1024 * 1024 * 1024}


def shorten_cfg(cmd):
    cfg = cmd.render("{soc_cfg}")
    if cfg.endswith(".cfg"):
        cfg = cfg[:-4]
    return cfg


class SoCValidation(PantheraCmd):
    panthera_cmd = """matrix.verify"""
    summary_file = f"{SUMMARY_CSV_SUFFIX}"
    verify_cmds = None

    cmd_params = ["--csv", "{summary_file}", "{verify_cmds}"]

    @classmethod
    def parser_add_args(cls, parser: ArgumentParser, **kwargs):
        PantheraCmd.parser_add_args(parser, **kwargs)


class SoCSim(SimCmd):
    sim = [
        "-max_time {max_time}",
        "-warmup_time {warmup_time}",
        "-sys.soc_0:cfg", Cmd.join_paths("{config_model}", "soc", "{soc_cfg}"),
    ]  # fmt: skip
    evp = [
        "-i {sample_interval}",
        "@chi_ops.cfg {evp_cfgs}",
        f"+summarize --summary {SUMMARY_CSV_SUFFIX} --timeseries {TIMESERIES_CSV_SUFFIX}",
        "{evp_plugins}",
    ]  # fmt: skip

    max_time = "500us"
    warmup_time = "0us"
    sample_interval = "1us"
    configuration = "{experiment}"
    short_cfg = shorten_cfg

    @classmethod
    def parser_add_args(cls, parser: ArgumentParser, **kwargs):
        super().parser_add_args(parser, **kwargs)
        soc_group = parser.add_argument_group(title="SoC simulation arguments")
        soc_group.add_argument("--max_time", default=SoCSim.max_time, help="Simulation time limit (default: 500us)")
        soc_group.add_argument(
            "--sample-interval", default=SoCSim.sample_interval, help="Metric sampling interval (default: 1us)"
        )
        soc_group.add_argument("--soc-cfg", help="SoC configuration")

    @property
    def validate_per(self) -> str:
        return "experiment"

    def validation_cmd(self, amount: int) -> Cmd:
        return (
            super()
            .validation_cmd(amount)
            .copy(
                max_time=f"{amount}ns",
                sim=self.sim + ["-event_stream_always", "true"],
            )
        )


def compute_stop_addr(cmd):
    m = SIZE_RE.match(cmd.render("{mem_size}"))
    if m:
        number, suffix = m.groups([1, 2])
        size = int(number) * SUFFIX_MUL[suffix]
    else:
        size = int(number)
    return int(cmd.render("{start_addr}")) + size


def compute_stop_addr_b(cmd):
    m = SIZE_RE.match(cmd.render("{mem_size}"))
    if m:
        number, suffix = m.groups([1, 2])
        size = int(number) * SUFFIX_MUL[suffix]
    else:
        size = int(number)
    return int(cmd.render("{stop_addr}")) + size


def compute_cache_lines(cmd):
    m = SIZE_RE.match(cmd.render("{mem_size}"))
    if m:
        number, suffix = m.groups([1, 2])
        size = int(number) * SUFFIX_MUL[suffix]
    else:
        size = int(number)
    return int(size / 64)


def compute_tlp_size(cmd):
    m = cmd.render("{TLP_size}")
    return int(int(m) / 64)


def compute_rtlp_size(cmd):
    m = cmd.render("{RTLP_size}")
    return int(int(m) / 64)


def compute_wtlp_size(cmd):
    m = cmd.render("{WTLP_size}")
    return int(int(m) / 64)


def compute_loop_size(cmd):
    """How large is the pointer-chasing loop given the start/stop addresses and
    assuming a 64-byte cache line granularity."""
    start = int(cmd.render("{start_addr}"))
    stop = int(cmd.render("{stop_addr}"))
    return (stop - start) // 64


class ReverseRelativeAddress:
    def __init__(self, start_addr_key, size_key):
        self.start_addr_key = "{" + start_addr_key + "}"
        self.size_key = "{" + size_key + "}"

    def __call__(self, cmd):
        number = cmd.render(self.size_key)
        m = SIZE_RE.match(number)
        if m:
            number, suffix = m.groups([1, 2])
            size = int(number) * SUFFIX_MUL[suffix]
        else:
            size = int(number)
        return int(cmd.render(self.start_addr_key)) - size


class AddressRegions:
    def __init__(self, param_prefix):
        self.param_prefix = param_prefix

    def __call__(self, cmd):
        if cmd.render("{prog}") == "copy":
            return [
                f"-{self.param_prefix}.start_addr_a", "{start_addr}",
                f"-{self.param_prefix}.stop_addr_a", "{stop_addr}",
                f"-{self.param_prefix}.start_addr_b", "{stop_addr}",
                f"-{self.param_prefix}.stop_addr_b", compute_stop_addr_b,
            ]  # fmt: skip
        else:
            return [
                f"-{self.param_prefix}.start_addr", "{start_addr}",
                f"-{self.param_prefix}.stop_addr", "{stop_addr}",
            ]  # fmt: skip


# fmt: off
lat_cmd = SoCSim(
    experiment    = "latency",
    name          = "latency_core{active_core}_{mem_size}",
    soc_cfg       = "dn-octant.cfg",
    active_core   = 0,
    mem_size      = "128M",
    warmup_size   = "{mem_size}",
    max_time      = "200us",
    start_addr    = 0x0020000,
    stop_addr     = compute_stop_addr,
    repeat_count  = compute_loop_size,
    enable_warmup = "false",
    high_stash_effort = "false",
    warmup_stop_addr = "{stop_addr}",
    warmup_start_addr = ReverseRelativeAddress("warmup_stop_addr", "warmup_size"),
    warmup_loops  = 1,
    sim           = SoCSim.sim + [
        r"-~sys\.soc_0\.core_[0-9]+\.enable", "true",
        r"-~sys\.soc_0\.core_[0-9]+\.stash_only", "true",
        "-sys.soc_0.core_{active_core}.stash_only", "false",
        "-sys.soc_0.core_{active_core}.source:cfg", "{config_model}/inj/prog_lat_mem_rd.cfg",
        "-sys.soc_0.core_{active_core}.source.start_addr", "{start_addr}",
        "-sys.soc_0.core_{active_core}.source.stop_addr", "{stop_addr}",
        "-sys.soc_0.core_{active_core}.source.enable_warmup", "{enable_warmup}",
        "-sys.soc_0.warmup.high_stash_effort", "{high_stash_effort}",
        "-sys.soc_0.core_{active_core}.source.warmup_start_addr", "{warmup_start_addr}",
        "-sys.soc_0.core_{active_core}.source.warmup_stop_addr", "{warmup_stop_addr}",
        "-sys.soc_0.core_{active_core}.source.repeat_count", "{repeat_count}",
    ],
    evp_cfgs      = ["@lat_bw_inj.cfg"],
    evp_plugins   = ["+python", "inj_ld_to_ld_lat.py", "--latency-core", "{active_core}",
                     "+python", "lat_breakdown.py", "--latency-core", "{active_core}"],
    directory     = "cfg.{short_cfg}/experiment.{experiment}/active_core.{active_core}/mem_size.{mem_size}"
)

ior_lat_cmd = SoCSim(
    experiment    = "ior_lat",
    name          = "latency_ior{active_ior}",
    soc_cfg       = "dn-hemi0.cfg",
    active_ior    = 0,
    mem_size      = "128M",
    max_time      = "500us",
    start_addr    = 0x0020000,
    stop_addr     = compute_stop_addr,
    sim           = SoCSim.sim + [
        r"-~sys\.soc_0\.core_[0-9]+\.enable", "true",
        r"-~sys\.soc_0\.core_[0-9]+\.stash_only", "true",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.brain_dead_limit", "1000000",
        "-sys.soc_0.iorinjector_{active_ior}.enable", "true",
        "-sys.soc_0.iorinjector_{active_ior}.source_0:cfg", "{config_model}/inj/prog_ior_lat.cfg",
        "-sys.soc_0.iorinjector_{active_ior}.source_0.start_addr", "{start_addr}",
        "-sys.soc_0.iorinjector_{active_ior}.source_0.stop_addr", "{stop_addr}"
    ],
    evp_cfgs      = ["@lat_bw_inj.cfg"],
    evp_plugins   = ["+python", "ior_roundtrip.py", "--latency-ior", "{active_ior}",
                     "+python", "lat_ior_breakdown.py", "--latency-ior", "{active_ior}", "--report-rdi"],
    directory     = "cfg.{short_cfg}/experiment.{experiment}/active_ior.{active_ior}"
)

ior_ld_lat_cmd = SoCSim(
    experiment    = "ior_ld_lat",
    name          = "latency_ior{active_ior}",
    soc_cfg       = "dn-hemi0.cfg",
    active_ior    = 0,
    mem_size      = "128M",
    max_time      = "500us",
    interval      = "interval",
    prog          = "1r0w",
    start_addr    = 0x0020000,
    stop_addr     = compute_stop_addr,
    sim           = SoCSim.sim + [
        r"-~sys\.soc_0\.core_[0-9]+\.enable", "true",
        r"-~sys\.soc_0\.core_[0-9]+\.stash_only", "false",
        r"-~sys\.soc_0\.core_[0-9]+\.source:cfg", "{config_model}/inj/prog_{prog}.cfg",
        r"-~sys\.soc_0\.core_[0-9]+\.source.interval", "{interval}",
        r"-~sys\.soc_0\.core_[0-9]+\.source.loop_count", "1000000",
        r"-~sys\.soc_0\.core_[0-9]+\.source\.paging\.connect", "sys.pager",
        r"-~sys\.soc_0\.core_[0-9]+\.brain_dead_limit", "1000000000",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.brain_dead_limit", "1000000",
        "-sys.soc_0.iorinjector_{active_ior}.enable", "true",
        "-sys.soc_0.iorinjector_{active_ior}.source_0:cfg", "{config_model}/inj/prog_ior_lat.cfg",
        "-sys.soc_0.iorinjector_{active_ior}.source_0.start_addr", "{start_addr}",
        "-sys.soc_0.iorinjector_{active_ior}.source_0.stop_addr", "{stop_addr}"
    ],
    evp_cfgs      = ["@lat_bw_inj.cfg"],
    evp_plugins   = ["+python", "ior_roundtrip.py", "--latency-ior", "{active_ior}",
                     "+python", "lat_ior_breakdown.py", "--latency-ior", "{active_ior}", "--report-rdi"],
    directory     = "cfg.{short_cfg}/experiment.{experiment}/active_ior.{active_ior}/interval.{interval}/workload.{prog}"
)

ior_bw_cmd = SoCSim(
    experiment      = "ior_bw",
    name            = "bw_ior{active_ior}_{prog}",
    soc_cfg         = "dn-hemi0.cfg",
    active_ior      = 0,
    prog            = "reads",
    RTLP_size       = "1024",
    WTLP_size       = "1024",
    mem_size        = "128M",
    warmup_size     = "4M",
    rtlp_size       = compute_rtlp_size,
    wtlp_size       = compute_wtlp_size,
    max_time        = "500us",
    start_addr      = 0x0020000,
    stop_addr       = compute_stop_addr,
    warmup_stop_addr = "{stop_addr}",
    warmup_start_addr = ReverseRelativeAddress("warmup_stop_addr", "warmup_size"),
    enable_warmup   = "false",
    high_stash_effort = "false",
    alloc           = "false",
    sim             = SoCSim.sim + [
        r"-~sys\.soc_0\.core_[0-9]+\.enable", "true",
        r"-~sys\.soc_0\.core_[0-9]+\.stash_only", "true",
        "-sys.soc_0.core_{active_ior}.source.warmup_only", "{enable_warmup}",
        "-sys.soc_0.core_{active_ior}.source:cfg", "{config_model}/inj/prog_warmup.cfg",
        "-sys.soc_0.core_{active_ior}.source.warmup_start_addr", "{warmup_start_addr}",
        "-sys.soc_0.core_{active_ior}.source.warmup_stop_addr", "{warmup_stop_addr}",
        #"-sys.soc_0.core_{active_ior}.source.paging.connect", "sys.pager",
        "-sys.soc_0.core_{active_ior}.source.region", "reg{active_ior}",
        "-sys.soc_0.warmup.high_stash_effort", "{high_stash_effort}",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.brain_dead_limit", "1000000",
        "-sys.soc_0.iorinjector_{active_ior}.enable", "true",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.source_[0-9]+:cfg", "{config_model}/inj/prog_ior_{prog}.cfg",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.source_[0-9]+\.start_addr", "{start_addr}",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.source_[0-9]+\.stop_addr", "{stop_addr}",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.source_[0-9]+\.rtlp_size", "{rtlp_size}",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.source_[0-9]+\.wtlp_size", "{wtlp_size}",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.source_[0-9]+\.region", "reg{active_ior}",
        #r"-~sys\.soc_0\.iorinjector_[0-9]+\.source_[0-9]+\.paging\.connect", "sys.pager",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.Allocate", "{alloc}"
    ],
    evp_cfgs      = ["@lat_bw_inj.cfg", "@mesh-congestion.cfg"],
    evp_plugins   = ["+python", "ior_roundtrip.py", "--latency-ior", "{active_ior}",
                     "+python", "lat_ior_breakdown.py", "--latency-ior", "{active_ior}", "--report-rdi"],
    directory     = "cfg.{short_cfg}/experiment.{experiment}/active_ior.{active_ior}/workload.{prog}/mem_size.{mem_size}"
)

ior_rwbw_cmd = SoCSim(
    experiment      = "ior_rwbw",
    name            = "bw_ior{active_ior}_{prog}_{rrepeat}_{wrepeat}_{axidelay}_{iot_size}",
    soc_cfg         = "dn-hemi0.cfg",
    active_ior      = 0,
    rrepeat         = 1,
    wrepeat         = 1,
    axidelay        = 1,
    iot_size        = 1024,
    prog            = "reads",
    RTLP_size       = "1024",
    WTLP_size       = "1024",
    mem_size        = "128M",
    warmup_size     = "3M",
    rtlp_size       = compute_rtlp_size,
    wtlp_size       = compute_wtlp_size,
    max_time        = "250us",
    start_addr      = 0x0020000,
    stop_addr       = compute_stop_addr,
    sim             = SoCSim.sim + [
        r"-~sys\.soc_0\.core_[0-9]+\.enable", "true",
        r"-~sys\.soc_0\.core_[0-9]+\.stash_only", "true",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.brain_dead_limit", "1000000",
        "-sys.soc_0.iorinjector_{active_ior}.enable", "true",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.source_[0-9]+:cfg", "{config_model}/inj/prog_ior_{prog}.cfg",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.source_[0-9]+\.rrepeat", "{rrepeat}",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.source_[0-9]+\.wrepeat", "{wrepeat}",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.source_[0-9]+\.start_addr", "{start_addr}",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.source_[0-9]+\.stop_addr", "{stop_addr}",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.source_[0-9]+\.rtlp_size", "{rtlp_size}",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.source_[0-9]+\.wtlp_size", "{wtlp_size}",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.pipe_delay", "{axidelay}",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.iord_size", "{iot_size}",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.iowr_size", "{iot_size}",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.source_[0-9]+\.paging\.connect", "sys.pager"
    ],
    evp_cfgs      = ["@lat_bw_inj.cfg", "@mesh-congestion.cfg"],
    evp_plugins   = ["+python", "ior_roundtrip.py", "--latency-ior", "{active_ior}",
                     "+python", "lat_ior_breakdown.py", "--latency-ior", "{active_ior}", "--report-rdi"],
    directory     = "cfg.{short_cfg}/experiment.{experiment}/active_ior.{active_ior}/workload.{prog}/repeat.{rrepeat}_{wrepeat}/tlp_size.{rtlp_size}_{wtlp_size}/axidelay.{axidelay}/iot_size.{iot_size}"
)

mixed_ior_bw_cmd = SoCSim(
    experiment      = "mixed_ior_bw",
    name            = "bw_ior{active_ior}",
    soc_cfg         = "dn-hemi0.cfg",
    active_ior      = 0,
    prog            = "reads",
    RTLP_size       = "1024",
    WTLP_size       = "1024",
    mem_size        = "128M",
    rtlp_size       = compute_rtlp_size,
    wtlp_size       = compute_wtlp_size,
    nrt_size        = 512,
    prt_size        = 512,
    max_time        = "250us",
    start_addr      = 0x0020000,
    stop_addr       = compute_stop_addr,
    sim             = SoCSim.sim + [
        r"-~sys\.soc_0\.core_[0-9]+\.enable", "true",
        r"-~sys\.soc_0\.core_[0-9]+\.stash_only", "true",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.brain_dead_limit", "1000000",
        "-sys.soc_0.iorinjector_{active_ior}.enable", "true",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.num_sources", "4",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.source_[0-1]:cfg", "{config_model}/inj/prog_ior_reads.cfg",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.source_[2-3]:cfg", "{config_model}/inj/prog_ior_writes.cfg",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.source_[0-9]+\.start_addr", "{start_addr}",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.source_[0-9]+\.stop_addr", "{stop_addr}",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.source_[0-1]\.rtlp_size", "{rtlp_size}",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.source_[2-3]\.wtlp_size", "{wtlp_size}",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.nrt_size", "{nrt_size}",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.prt_size", "{prt_size}",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.source_[0-9]+\.paging\.connect", "sys.pager"
    ],
    evp_cfgs      = ["@lat_bw_inj.cfg", "@mesh-congestion.cfg"],
    evp_plugins   = ["+python", "ior_roundtrip.py", "--latency-ior", "{active_ior}"],
    directory     = "cfg.{short_cfg}/experiment.{experiment}/active_ior.{active_ior}/workload.{prog}/tlp_size.{rtlp_size}_{wtlp_size}/tracker_size.{nrt_size}"
)

multi_ior_bw_cmd = SoCSim(
    experiment      = "multi_ior_bw",
    name            = "multi_ior_bw",
    soc_cfg         = "dn-hemi0.cfg",
    prog            = "reads",
    TLP_size        = "1024",
    mem_size        = "128M",
    iors            = "0",
    tlp_size        = compute_tlp_size,
    max_time        = "100us",
    start_addr      = 0x0020000,
    stop_addr       = compute_stop_addr,
    sim             = SoCSim.sim + [
        r"-~sys\.soc_0\.core_[0-9]+\.enable", "true",
        r"-~sys\.soc_0\.core_[0-9]+\.stash_only", "true",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.brain_dead_limit", "1000000",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.source_[0-9]+:cfg", "{config_model}/inj/prog_ior_{prog}.cfg",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.source_[0-9]+\.start_addr", "{start_addr}",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.source_[0-9]+\.stop_addr", "{stop_addr}",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.source_[0-9]+\.rtlp_size", "{tlp_size}",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.source_[0-9]+\.wtlp_size", "{tlp_size}",
        r"-~sys\.soc_0\.iorinjector_[0-9]+\.source_[0-9]+\.paging\.connect", "sys.pager"
    ],
    evp_cfgs      = ["@lat_bw_inj.cfg","@mesh-congestion.cfg"],
    directory     = "cfg.{short_cfg}/experiment.{experiment}/workload.multi/tlp_size.{tlp_size}/iors.{iors}"
)

bw_cmd = SoCSim(
    experiment      = "bw",
    name            = "bw_core{active_core}_{prog}_{mem_size}",
    soc_cfg         = "dn-octant.cfg",
    active_core     = 0,
    prog            = "1r0w",
    mem_size        = "128M",
    warmup_size     = "{mem_size}",
    max_time        = "500us",
    start_addr      = 0x0020000,
    stop_addr       = compute_stop_addr,
    stride          = 64,
    address_regions = AddressRegions("sys.soc_0.core_{active_core}.source"),
    enable_warmup   = "false",
    high_stash_effort = "false",
    warmup_stop_addr = "{stop_addr}",
    warmup_start_addr = ReverseRelativeAddress("warmup_stop_addr", "warmup_size"),
    sim             = SoCSim.sim + [
        r"-~sys\.soc_0\.core_[0-9]+\.enable", "true",
        r"-~sys\.soc_0\.core_[0-9]+\.stash_only", "true",
        "-sys.soc_0.core_{active_core}.stash_only", "false",
        "-sys.soc_0.core_{active_core}.source:cfg", "{config_model}/inj/prog_{prog}.cfg",
        "-sys.soc_0.core_{active_core}.source.interval", "0",
        "-sys.soc_0.core_{active_core}.source.stride", "{stride}",
        #"-sys.soc_0.core_{active_core}.source.paging.connect", "sys.pager",
        "-sys.soc_0.core_{active_core}.source.enable_warmup", "{enable_warmup}",
        "-sys.soc_0.warmup.high_stash_effort", "{high_stash_effort}",
        "-sys.soc_0.core_{active_core}.source.warmup_start_addr", "{warmup_start_addr}",
        "-sys.soc_0.core_{active_core}.source.warmup_stop_addr", "{warmup_stop_addr}",
        "{address_regions}"
    ],
    evp_cfgs        = ["@lat_bw_inj.cfg"],
    evp_plugins     = ["+python", "lat_breakdown.py", "--latency-core", "{active_core}"],
    directory       = "cfg.{short_cfg}/experiment.{experiment}/active_core.{active_core}/workload.{prog}/mem_size.{mem_size}"
)

multiload_cmd = SoCSim(
    experiment      = "multiload",
    name            = "multiload_lat{latency_core}_agg{nagg}_{prog}_{mem_size}",
    soc_cfg         = "dn-octant.cfg",
    latency_core    = 0,
    nagg            = 0,
    prog            = "1r0w",
    mem_size        = "128M",
    max_time        = "1ms",
    warmup_time     = "500us",
    start_addr      = 0x0020000,
    stop_addr       = compute_stop_addr,
    address_regions = AddressRegions(r"~sys\.soc_0\.core_[0-9]+\.source"),
    enable_warmup   = "false",
    sim             = SoCSim.sim + [
        r"-~sys\.soc_0\.core_[0-9]+\.enable", "true",
        r"-~sys\.soc_0\.core_[0-9]+\.stash_only", "true",
        r"-~sys\.soc_0\.core_[0-9]+\.source:cfg", "{config_model}/inj/prog_{prog}.cfg",
        r"-~sys\.soc_0\.core_[0-9]+\.source.interval", "0",
        "{address_regions}",
        r"-~sys\.soc_0\.core_[0-9]+\.source\.paging\.connect", "sys.pager",
        r"-~sys\.soc_0\.core_[0-9]+\.brain_dead_limit", "1000000",
        "-sys.soc_0.core_{latency_core}.stash_only", "false",
        "-sys.soc_0.core_{latency_core}.source:cfg", "{config_model}/inj/prog_chase.cfg",
        "-sys.soc_0.core_{latency_core}.source.enable_warmup", "{enable_warmup}"
    ],
    evp_cfgs        = ["@lat_bw_inj.cfg", "@idt.cfg", "@mesh-congestion.cfg", "@cbusy.cfg"],
    evp_plugins     = ["+python", "inj_req_to_req_lat.py", "--latency-core", "{latency_core}",
                       "+python", "lat_breakdown.py", "--latency-core", "{latency_core}"],
    directory       = "cfg.{short_cfg}/experiment.{experiment}/latency_core.{latency_core}/agg.{nagg}/workload.{prog}/mem_size.{mem_size}"
)

bb_cmd = SoCSim(
    experiment      = "bb",
    name            = "bb_lat{latency_core}_{interval}_{prog}_{mem_size}",
    soc_cfg         = "dn-octant.cfg",
    latency_core    = 0,
    prog_latancy_core = "chase",
    interval        = "interval",
    mem_size        = "128M",
    warmup_size     = "4M",
    max_time        = "4ms",
    warmup_time     = "2ms",
    start_addr      = 0x0020000,
    stop_addr       = compute_stop_addr,
    warmup_stop_addr = "{stop_addr}",
    warmup_start_addr = ReverseRelativeAddress("warmup_stop_addr", "warmup_size"),
    address_regions = AddressRegions(r"~sys\.soc_0\.core_[0-9]+\.source"),
    enable_warmup   = "false",
    enable_cbusy    = "false",
    bank_hash_bits  = 0,
    sim             = SoCSim.sim + [
         r"-~sys\.soc_0\.core_[0-9]+\.enable", "true",
        r"-~sys\.soc_0\.core_[0-9]+\.source:cfg", "{config_model}/inj/prog_{prog}.cfg",
        r"-~sys\.soc_0\.core_[0-9]+\.source.interval", "{interval}",
        r"-~sys\.soc_0\.core_[0-9]+\.source.loop_count", "1000000",
        r"-~sys\.soc_0\.core_[0-9]+\.source\.paging\.connect", "sys.pager",
        r"-~sys\.soc_0\.core_[0-9]+\.source\.enable_events_on_start", "true",
        r"-~sys\.soc_0\.core_[0-9]+\.source\.enable_warmup", "true",
        r"-~sys\.soc_0\.core_[0-9]+\.brain_dead_limit", "1000000000",
        "-sys.soc_0.core_{latency_core}.source:cfg", "{config_model}/inj/prog_{prog_latency_core}.cfg",
        "-sys.soc_0.core_{latency_core}.source.enable_warmup", "{enable_warmup}",
        r"-~sys\.soc_0\.core_[0-9]+\.source\.warmup_start_addr", "{warmup_start_addr}",
        r"-~sys\.soc_0\.core_[0-9]+\.source\.warmup_stop_addr", "{warmup_stop_addr}",
        r"-~sys\.soc_0\.core_[0-9]+\.cbusy\.enable", "{enable_cbusy}",
        r"-~sys\.soc_0\.core_[0-9]+\.bank_hash_bits", "{bank_hash_bits}",
        #r"-~sys\.soc_0\.memory_[0-9]+\.chi_interface\.cbusy\.enable", "{enable_cbusy}",
        r"-~sys\.soc_0\.pxha_[0-9]+\.cbusy\.enable", "{enable_cbusy}",
        r"-~sys\.soc_0\.pmha_[0-9]+\.cbusy\.enable", "{enable_cbusy}",
        "{address_regions}"
    ],
    evp_cfgs      = ["@lat_bw_inj.cfg", "@idt.cfg", "@mesh-congestion.cfg", "@cbusy.cfg"],
    evp_plugins   = ["+python", "inj_req_to_req_lat.py", "--latency-core", "{latency_core}",
                     "+python", "lat_breakdown.py", "--latency-core", "{latency_core}"],
    directory     = "cfg.{short_cfg}/experiment.{experiment}/latency_core.{latency_core}/interval.{interval}/workload.{prog}/mem_size.{mem_size}"
)

multicore_cmd = SoCSim(
    experiment      = "multicore",
    name            = "multicore_{mem_size}_{prog}",
    soc_cfg         = "dn-octant.cfg",
    mem_size        = "128M",
    warmup_size     = "{mem_size}",
    max_time        = "4ms",
    warmup_time     = "1us",
    start_addr      = 0x0020000,
    stop_addr       = compute_stop_addr,
    warmup_stop_addr = "{stop_addr}",
    warmup_start_addr = ReverseRelativeAddress("warmup_stop_addr", "warmup_size"),
    stride          = 64,
    address_regions = AddressRegions(r"~sys\.soc_0\.core_[0-9]+\.source"),
    enable_warmup   = "false",
    bank_hash_bits  = 0,
    enable_cbusy    = "false",
    sim             = SoCSim.sim + [
        r"-~sys\.soc_0\.core_[0-9]+\.enable", "true",
        r"-~sys\.soc_0\.core_[0-9]+\.stash_only", "true",
        r"-~sys\.soc_0\.core_[0-9]+\.source:cfg", "{config_model}/inj/prog_{prog}.cfg",
        r"-~sys\.soc_0\.core_[0-9]+\.source.loop_count", "1000000",
        r"-~sys\.soc_0\.core_[0-9]+\.source\.paging\.connect", "sys.pager",
        r"-~sys\.soc_0\.core_[0-9]+\.source\.enable_events_on_start", "true",
        r"-~sys\.soc_0\.core_[0-9]+\.source\.enable_warmup", "{enable_warmup}",
        r"-~sys\.soc_0\.core_[0-9]+\.source\.warmup_start_addr", "{warmup_start_addr}",
        r"-~sys\.soc_0\.core_[0-9]+\.source\.warmup_stop_addr", "{warmup_stop_addr}",
        r"-~sys\.soc_0\.core_[0-9]+\.source\.stride", "{stride}",
        r"-~sys\.soc_0\.core_[0-9]+\.brain_dead_limit", "1000000",
        r"-~sys\.soc_0\.core_[0-9]+\.bank_hash_bits", "{bank_hash_bits}",
        r"-~sys\.soc_0\.core_[0-9]+\.cbusy\.enable", "{enable_cbusy}",
        r"-~sys\.soc_0\.memory_[0-9]+\.chp\.cbusy\.enable", "{enable_cbusy}",
        r"-~sys\.soc_0\.memory_[0-9]+\.chp\.write_cbusy\.enable", "{enable_cbusy}",
        r"-~sys\.soc_0\.pxha_[0-9]+\.cbusy\.enable", "{enable_cbusy}",
        r"-~sys\.soc_0\.pmha_[0-9]+\.cbusy\.enable", "{enable_cbusy}",
        "{address_regions}"
    ],
    evp_cfgs      = ["@lat_bw_inj.cfg", "@mesh-congestion.cfg"],
    directory     = "cfg.{short_cfg}/experiment.{experiment}/workload.{prog}/mem_size.{mem_size}"
)

c2c_lat_cmd = SoCSim(
    experiment    = "c2c_latency",
    name          = "c2c_latency_{core1}_{core2}",
    soc_cfg       = "dn-quad1.cfg",
    mem_size      = "128M",
    core1         = "core1",
    core2         = "core2",
    address_list  = "address_list",
    max_time      = "500us",
    warmup_time   = "5us",
    enable_warmup = "false",
    enable_events_on_start = "true",
    mask          = "mask",
    sim           = SoCSim.sim + [
        r"-~sys\.soc_0\.core_[0-9]+\.enable", "true",
        r"-~sys\.soc_0\.core_[0-9]+\.stash_only", "true",
        r"-~sys\.soc_0\.core_[0-9]+\.data_offset", "0x0",
        r"-~sys\.soc_0\.core_[0-9]+\.source:cfg", "{config_model}/inj/prog_c2c_latency.cfg",
        r"-~sys\.soc_0\.core_[0-9]+\.source.enable_warmup", "{enable_warmup}",
        r"-~sys\.soc_0\.core_[0-9]+\.source.enable_events_on_start", "{enable_events_on_start}",
        r"-~sys\.soc_0\.core_[0-9]+\.source.ping_pong_mask", "{mask}",
        r"-~sys\.soc_0\.core_[0-9]+\.source.address_list", "{address_list}"
    ],
    evp_cfgs      = ["@lat_bw_inj.cfg", "@lat-breakdown.cfg"],
    evp_plugins   = ["+python", "inj_req_to_req_lat.py", "--req-opcode", "ReadUnique"],
    directory     = "cfg.{short_cfg}/experiment.{experiment}/latency_core.{core1}_{core2}/workload.c2c_lat/mem_size.{mem_size}"
)

c2c_ld_lat_cmd = SoCSim(
    experiment    = "c2c_ld_latency",
    name          = "c2c_ld_latency_{core1}_{core2}",
    soc_cfg       = "dn-quad1.cfg",
    mem_size      = "128M",
    core1         = "core1",
    core2         = "core2",
    address_list  = "address_list",
    max_time      = "500us",
    warmup_time   = "5us",
    enable_warmup = "false",
    enable_events_on_start = "true",
    mask          = "mask",
    interval      = "interval",
    prog          = "1r0w",
    sim           = SoCSim.sim + [
        r"-~sys\.soc_0\.core_[0-9]+\.enable", "true",
        r"-~sys\.soc_0\.core_[0-9]+\.stash_only", "false",
        r"-~sys\.soc_0\.core_[0-9]+\.source:cfg", "{config_model}/inj/prog_{prog}.cfg",
        r"-~sys\.soc_0\.core_[0-9]+\.source.interval", "{interval}",
        "-sys.soc_0.core_{core1}.source:cfg", "{config_model}/inj/prog_c2c_latency.cfg",
        "-sys.soc_0.core_{core2}.source:cfg", "{config_model}/inj/prog_c2c_latency.cfg",
        r"-~sys\.soc_0\.core_[0-9]+\.source.enable_warmup", "{enable_warmup}",
        r"-~sys\.soc_0\.core_[0-9]+\.source.enable_events_on_start", "{enable_events_on_start}",
        r"-~sys\.soc_0\.core_[0-9]+\.source.ping_pong_mask", "{mask}",
        r"-~sys\.soc_0\.core_[0-9]+\.source.address_list", "{address_list}"
    ],
    evp_cfgs      = ["@lat_bw_inj.cfg", "@lat-breakdown.cfg"],
    evp_plugins   = ["+python", "inj_req_to_req_lat.py", "--req-opcode", "ReadUnique"],
    directory     = "cfg.{short_cfg}/experiment.{experiment}/latency_core.{core1}_{core2}/interval.{interval}/workload.{prog}/mem_size.{mem_size}"
)

c2c_bw_cmd = SoCSim(
    experiment    = "c2c_bw",
    name          = "c2c_bw_{core1}_{core2}",
    soc_cfg       = "dn-quad1.cfg",
    core1         = "core1",
    core2         = "core2",
    mem_size      = "2M",
    max_time      = "500us",
    start_addr    = 0x0020000,
    stop_addr     = compute_stop_addr,
    enable_warmup = "false",
    interval      = compute_cache_lines,
    mask          = "mask",
    sim           = SoCSim.sim + [
        r"-~sys\.soc_0\.core_[0-9]+\.enable", "true",
        r"-~sys\.soc_0\.core_[0-9]+\.stash_only", "true",
        r"-~sys\.soc_0\.core_[0-9]+\.data_offset", "0x0",
        r"-~sys\.soc_0\.core_[0-9]+\.source:cfg", "{config_model}/inj/prog_c2c_bw.cfg",
        r"-~sys\.soc_0\.core_[0-9]+\.source.interval", "{interval}",
        r"-~sys\.soc_0\.core_[0-9]+\.source.start_addr", "{start_addr}",
        r"-~sys\.soc_0\.core_[0-9]+\.source.stop_addr", "{stop_addr}",
        r"-~sys\.soc_0\.core_[0-9]+\.source.enable_warmup", "{enable_warmup}",
        r"-~sys\.soc_0\.core_[0-9]+\.source.enable_events_on_start", "{enable_events_on_start}",
        r"-~sys\.soc_0\.core_[0-9]+\.source.ping_pong_mask", "{mask}",
        r"-~sys\.soc_0\.core_[0-9]+\.brain_dead_limit", "1000000000"
    ],
    evp_cfgs      = ["@lat_bw_inj.cfg", "@lat-breakdown.cfg"],
    evp_plugins   = ["+python", "inj_to_inj_bw.py"],
    directory     = "cfg.{short_cfg}/experiment.{experiment}/latency_core.{core1}_{core2}/interval.{interval}/workload.c2c_bw/mem_size.{mem_size}"
)
# fmt: on
