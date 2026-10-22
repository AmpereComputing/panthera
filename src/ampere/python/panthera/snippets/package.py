#!/usr/bin/env python3
# Copyright (c) 2020-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Package ELF, instruction/memory streams into tracelist.

Process a number of ELF, .is.xz, and .ms.xz files into a "release" for consumption by
both RTL and panthera
"""

import argparse
import gzip
import hashlib
import itertools
import json
import os
import re
import shutil
import subprocess
import sys
from collections import namedtuple
from datetime import datetime
from multiprocessing import Pool, cpu_count
from pathlib import Path

from .. import install_dir


def sha256sum(filename):
    sha256_sum = hashlib.sha256()
    with open(filename, "rb") as fp:
        chunk = fp.read(4096)
        while chunk:
            sha256_sum.update(chunk)
            chunk = fp.read(4096)
        return sha256_sum.hexdigest()


def get_is_info(is_filename):
    is_info = os.path.join(install_dir, "bin", "is_info")
    stdout = subprocess.check_output([is_info, is_filename]).decode()
    info_re = re.compile(r"^\s*\(([0-9]+) warmup, ([0-9]+) measurement, and ([0-9]+) cooldown\)\s*$")
    for line in stdout.split("\n"):
        m = info_re.match(line)
        if m:
            return {
                "warmup_instructions": int(m.group(1)),
                "measurement_instructions": int(m.group(2)),
                "cooldown_instructions": int(m.group(3)),
            }

    raise Exception("is_info did not return valid output")


def generate_l2c_preload(full_memory_stream, l2c_warmup_stream, rtl_l2c_warmup_stream, soc):
    ms_warmup_bin = os.path.join(install_dir, "bin", "ms_to_l2c_warmup")
    subprocess.check_output([ms_warmup_bin, "--soc", soc, full_memory_stream, l2c_warmup_stream])

    rtl_l2c_warmup_bin = os.path.join(install_dir, "bin", "l2c_warmup_to_rtl")
    subprocess.check_output([rtl_l2c_warmup_bin, l2c_warmup_stream, rtl_l2c_warmup_stream])


def parse_args():
    def get_config(soc):
        return os.path.join(install_dir, "config", "model", "sys", f"{soc}-core-rtl.cfg")

    default_soc = "rogue"
    rtl_pan_cfg = get_config(default_soc)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-o", "--output-dir", default="./snippet_release", help="Output directory for snippet release")
    parser.add_argument("-s", "--suite", default="snippets", help="Suite name for these traces")
    parser.add_argument(
        "-n", "--ninst", type=int, default=100000, help="Number of instructions to simulate. Default: 100,000"
    )
    parser.add_argument("-c", "--config", default=None, help=f"Config file to use. Default config is {rtl_pan_cfg}")
    parser.add_argument("-w", "--no-warmup", default=False, action="store_true", help="Do not do L2 warmup")
    parser.add_argument(
        "-e",
        "--extra",
        default=[],
        action="append",
        nargs="+",
        help="List of extra args for panthera simulation testing",
    )
    parser.add_argument(
        "--threads",
        type=int,
        default=max(1, int(cpu_count() / 2)),
        help="Number of parallel processes to use for packaging snippets",
    )
    parser.add_argument(
        "--soc",
        default=default_soc,
        choices=["siryn", "banshee", "rogue", "domino"],
        help=f"SOC name to use for L2 configuration. Default: {default_soc}",
    )
    parser.add_argument("workload", help="Workload name for these files")
    parser.add_argument("files", help=".elf/.is.xz/.ms.xz files to create a release directory for", nargs="+")
    args = parser.parse_args()
    args.extra = list(itertools.chain(*args.extra))  # flatten regular list of lists
    if not args.config:
        args.config = get_config(args.soc)
    if not os.path.exists(args.config):
        print(f"Cannot find config file: {args.config}")
        sys.exit(1)

    # Replace 'files' with a list of dictionaries containing the relevant files
    # called 'traces'
    files = args.files
    del args.files
    args.traces = []
    seen_is = {}

    for f in files:
        dirname, base = os.path.split(f)
        basename = Path(base).resolve()
        if ".xz" == basename.suffix:
            basename = Path(basename.stem)
        if basename.suffix not in [".elf", ".is", ".xz"]:
            raise Exception(f"File extension not recognized: {f}")
        stem = basename.stem
        elf_filename = os.path.join(dirname, stem + ".elf")
        if not os.path.exists(elf_filename):
            elf_filename = os.path.join(dirname, stem + ".elf.gz")
            if not os.path.exists(elf_filename):
                raise Exception(f"Cannot find matching ELF file for: {f}")
        bs_filename = os.path.join(dirname, stem + ".bs.xz")
        is_filename = os.path.join(dirname, stem + ".is.xz")
        if not os.path.exists(is_filename):
            is_filename = os.path.join(dirname, stem + ".is")
            if not os.path.exists(is_filename):
                raise Exception(f"Cannot find matching IS file for: {f}")

        trace = {
            "elf_file": elf_filename,
            "pan_branch_warmup_file": bs_filename,
            "file": is_filename,
            "workload": args.workload,
        }

        if not args.no_warmup:
            ms_filename = os.path.join(dirname, stem + ".ms.xz")
            if os.path.exists(ms_filename):
                trace["full_offline_warmup_file"] = ms_filename
            else:
                print(f"Warning: warm start memory stream missing: {ms_filename}")

        iss_terminal_filename = os.path.join(dirname, stem + ".iss_terminal")
        if os.path.exists(iss_terminal_filename):
            trace["iss_terminal_file"] = iss_terminal_filename
        cosim_sysreg_filename = os.path.join(dirname, stem + ".cosim_sysregs")
        if os.path.exists(cosim_sysreg_filename):
            trace["cosim_sysreg_file"] = cosim_sysreg_filename

        if is_filename not in seen_is:
            args.traces.append(trace)
        seen_is[is_filename] = True

    return args


PackageTraceArgs = namedtuple(
    "PackageTraceArgs", ["output_dir", "suite", "config", "extra", "datestring", "soc", "ninst"]
)


def package_trace(pool_input):
    args, trace = pool_input
    errors = []

    is_info = get_is_info(trace["file"])
    # In the emitted manifest, we choose to limit the length of the
    # trace to the validated portion of the trace, args.ninst. If
    # trace is shorter than args.ninst, then the validation done in
    # this script will still fail and emit a message.
    trace["measurement_instructions"] = args.ninst
    trace.update(is_info)
    trace["sha256sum"] = sha256sum(trace["file"])
    if "full_offline_warmup_file" in trace:
        trace["full_offline_warmup_sha256sum"] = sha256sum(trace["full_offline_warmup_file"])

    _, base = os.path.split(trace["file"])
    stem = Path(Path(base).resolve().stem).stem
    suffixes = Path(base).resolve().suffixes
    if len(suffixes) > 0 and suffixes[-1] == ".is":
        ext = ".is"
    elif len(suffixes) > 1 and suffixes[-2] == ".is" and suffixes[-1] == ".xz":
        ext = ".is.xz"
    else:
        ext = "".join(suffixes)
    is_sha256sum = trace["sha256sum"][:6]
    is_base = f"{stem}_{args.datestring}_{is_sha256sum}{ext}"
    bs_base = f"{stem}_{args.datestring}_{is_sha256sum}.bs.xz"
    elf_base = f"{stem}_{args.datestring}_{is_sha256sum}.elf"
    iss_terminal_base = f"{stem}_{args.datestring}_{is_sha256sum}.iss_terminal"
    cosim_sysreg_base = f"{stem}_{args.datestring}_{is_sha256sum}.cosim_sysreg"

    workload_dir = os.path.join(args.output_dir, trace["workload"])

    is_dest = os.path.join(workload_dir, is_base)
    bs_dest = os.path.join(workload_dir, bs_base)
    elf_dest = os.path.join(workload_dir, elf_base)
    iss_terminal_dest = os.path.join(workload_dir, iss_terminal_base)
    cosim_sysreg_dest = os.path.join(workload_dir, cosim_sysreg_base)

    shutil.copyfile(trace["file"], is_dest)
    if trace["elf_file"].endswith(".gz"):
        with gzip.open(trace["elf_file"], "rb") as gzelf:
            with open(elf_dest, "wb") as elf:
                elf.write(gzelf.read())
    else:
        shutil.copyfile(trace["elf_file"], elf_dest)
    trace["elf_sha256sum"] = sha256sum(elf_dest)

    shutil.copyfile(trace["pan_branch_warmup_file"], bs_dest)
    trace["pan_branch_warmup_file_sha256sum"] = sha256sum(bs_dest)
    if "iss_terminal_file" in trace:
        shutil.copyfile(trace["iss_terminal_file"], iss_terminal_dest)
    if "cosim_sysreg_file" in trace:
        shutil.copyfile(trace["cosim_sysreg_file"], cosim_sysreg_dest)

    is_rel_dest = os.path.join(trace["workload"], is_base)
    bs_rel_dest = os.path.join(trace["workload"], bs_base)
    elf_rel_dest = os.path.join(trace["workload"], elf_base)
    iss_terminal_rel_dest = os.path.join(trace["workload"], iss_terminal_base)
    cosim_sysreg_rel_dest = os.path.join(trace["workload"], cosim_sysreg_base)
    trace["file"] = is_rel_dest
    trace["pan_branch_warmup_file"] = bs_rel_dest
    trace["elf_file"] = elf_rel_dest
    if "iss_terminal_file" in trace:
        trace["iss_terminal_file"] = iss_terminal_rel_dest
    if "cosim_sysreg_file" in trace:
        trace["cosim_sysreg_file"] = cosim_sysreg_rel_dest
    trace["name"] = stem
    trace["weight"] = "1.0"
    trace["suite"] = args.suite

    validated = False
    is_validate_logfilename = os.path.join(workload_dir, f"{stem}_{args.datestring}_{is_sha256sum}.is_validate.log")
    with open(is_validate_logfilename, "w") as is_validate_logfile:
        is_validate_proc = subprocess.run(
            [os.path.join(install_dir, "bin", "is_validate"), is_dest],
            stderr=subprocess.STDOUT,
            stdout=is_validate_logfile,
        )
        validated = is_validate_proc.returncode == 0

    if not validated:
        errors.append(f"VALIDATION FAILED: {is_dest}\n\tSee: {is_validate_logfile} for details of error.")

    if "full_offline_warmup_file" in trace:
        ms_base = f"{stem}_{args.datestring}_{is_sha256sum}.ms.xz"
        l2c_base = f"{stem}_{args.datestring}_{is_sha256sum}.l2c.ms.xz"
        rtl_l2c_base = f"{stem}_{args.datestring}_{is_sha256sum}.l2c.pav"

        ms_dest = os.path.join(workload_dir, ms_base)
        l2c_dest = os.path.join(workload_dir, l2c_base)
        rtl_l2c_dest = os.path.join(workload_dir, rtl_l2c_base)

        shutil.copyfile(trace["full_offline_warmup_file"], ms_dest)

        generate_l2c_preload(ms_dest, l2c_dest, rtl_l2c_dest, args.soc)
        trace["pan_cache_preload_file_sha256sum"] = sha256sum(l2c_dest)
        trace["rtl_cache_preload_file_sha256sum"] = sha256sum(rtl_l2c_dest)

        # Run through 'panthera sim' with
        # '-sys.soc_0.core_0.l2c.check_warmstart_overruns true' to ensure the memory
        # stream doesn't overrun the L2C (and also that everything else is sane
        # enough to run *something*)
        panthera = os.path.join(install_dir, "bin", "panthera")
        amperesys = os.path.join(install_dir, "bin", "amperesys")
        sim_logfilename = os.path.join(workload_dir, f"{stem}_{args.datestring}_{is_sha256sum}.panthera.log")

        panthera_cmd = [
            panthera, "sim", "--verbose", amperesys,
            "-sys.soc_0.core_0.instruction_stream_name", is_dest,
            "-sys.soc_0.core_0.l2c.warmup_ms", l2c_dest,
            "-cfg", args.config,
            "-sys.soc_0.core_0.gpc.max_instructions", str(args.ninst),
            "-sys.soc_0.core_0.gpc.heartbeat", "5000",
            "-sys.soc_0.core_0.l2c.check_warmstart_overruns", "true",
        ] + args.extra  # fmt: skip

        sim_ok = True
        with open(sim_logfilename, "w") as sim_logfile:
            proc = subprocess.run(panthera_cmd, stderr=subprocess.STDOUT, stdout=sim_logfile)
            sim_ok = proc.returncode == 0
        validated = validated and sim_ok
        if not sim_ok:
            errors.append(f"PANTHERA SIM CHECK FAILED: {is_dest}\n\tSee: {sim_logfile} for details of error.")

        ms_rel_dest = os.path.join(trace["workload"], ms_base)
        l2c_rel_dest = os.path.join(trace["workload"], l2c_base)
        rtl_l2c_rel_dest = os.path.join(trace["workload"], rtl_l2c_base)

        trace["full_offline_warmup_file"] = ms_rel_dest
        trace["pan_cache_preload_file"] = l2c_rel_dest
        trace["rtl_cache_preload_file"] = rtl_l2c_rel_dest

    return (trace, errors, validated)


def main():
    args = parse_args()

    datestring = datetime.today().strftime("%Y%m%d")
    Path(args.output_dir).mkdir(parents=True, exist_ok=True)

    # Create all workload-level directories before multiprocessing
    for trace in args.traces:
        workload_dir = os.path.join(args.output_dir, trace["workload"])
        Path(workload_dir).mkdir(parents=True, exist_ok=True)

    good_traces = []
    bad_traces = []
    seen_is_shas = set()

    print(f"Packaging {len(args.traces)} traces with {args.threads}-process pool...")
    packaged_traces = []
    with Pool(args.threads) as p:
        package_args = PackageTraceArgs(
            args.output_dir, args.suite, args.config, args.extra, datestring, args.soc, args.ninst
        )
        inputs = [(package_args, t) for t in args.traces]
        results = p.imap_unordered(package_trace, inputs, 1)
        packaged_traces = [r for r in results]

    # Package individual traces
    all_errors = []
    for new_trace, errors, validated in packaged_traces:
        seen_is_shas.add(new_trace["sha256sum"])

        if len(errors) > 0:
            all_errors += errors
            print(f"Errors processing trace {new_trace['file']}:")
            for error in errors:
                print(error)
            bad_traces.append(new_trace)
        elif validated:
            good_traces.append(new_trace)
        else:
            print(
                f"Error: Instruction stream validation failed for {new_trace['file']}, see its *.is_validate.log file"
            )
            bad_traces.append(new_trace)

    # Write traces to manifest.json or manifest.json.bad depending on whether
    # they passed or failed validation
    manifest_path = os.path.join(args.output_dir, "manifest.json")
    if os.path.exists(manifest_path):
        with open(manifest_path) as fp:
            old_traces = json.load(fp)
            for t in old_traces:
                if t["sha256sum"] not in seen_is_shas:
                    seen_is_shas.add(t["sha256sum"])
                    good_traces.append(t)

    manifest_path = os.path.join(args.output_dir, "manifest.json.bad")
    if os.path.exists(manifest_path):
        with open(manifest_path) as fp:
            old_traces = json.load(fp)
            for t in old_traces:
                if t["sha256sum"] not in seen_is_shas:
                    seen_is_shas.add(t["sha256sum"])
                    bad_traces.append(t)

    with open(os.path.join(args.output_dir, "manifest.json"), "w") as fp:
        json.dump(good_traces, fp, sort_keys=True, indent=4)

    with open(os.path.join(args.output_dir, "manifest.json.bad"), "w") as fp:
        json.dump(bad_traces, fp, sort_keys=True, indent=4)

    if len(all_errors) > 0:
        sys.exit(1)
    sys.exit(0)


if __name__ == "__main__":
    main()
