#!/usr/bin/env python3
# Copyright (c) 2020-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Regenerate the L2C warmup for an existing snippet release (for both RTL and panthera).

This overwrites existing warmup collateral, so please run this on a copy of a
release directory if you want to preserve the original.
"""

import argparse
import json
import os
import subprocess
from datetime import datetime
from pathlib import Path

from .. import install_dir
from . import package


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("release_dir", help="Existing release directory")
    args = parser.parse_args()

    datestring = datetime.today().strftime("%Y%m%d")

    manifest_path = os.path.join(args.release_dir, "manifest.json")
    traces = []
    with open(manifest_path) as fp:
        traces = json.load(fp)

    good_traces = []
    bad_traces = []

    for trace in traces:
        validated = False
        if "full_offline_warmup_file" in trace:
            is_path = os.path.join(args.release_dir, trace["file"])
            ms_path = os.path.join(args.release_dir, trace["full_offline_warmup_file"])
            l2c_path = os.path.join(args.release_dir, trace["pan_cache_preload_file"])
            rtl_l2c_path = os.path.join(args.release_dir, trace["rtl_cache_preload_file"])

            if os.path.exists(l2c_path):
                os.remove(l2c_path)
            if os.path.exists(rtl_l2c_path):
                os.remove(rtl_l2c_path)

            package.generate_l2c_preload(ms_path, l2c_path, rtl_l2c_path, "domino")
            trace["pan_cache_preload_file_sha256sum"] = package.sha256sum(l2c_path)
            trace["rtl_cache_preload_file_sha256sum"] = package.sha256sum(rtl_l2c_path)

            # Run through 'panthera sim' with
            # '-sys.soc_0.core_0.l2c.check_warmstart_overruns true' to ensure the memory
            # stream doesn't overrun the L2C (and also that everything else is sane
            # enough to run *something*)
            panthera = os.path.join(install_dir, "bin", "panthera")
            amperesys = os.path.join(install_dir, "bin", "amperesys")
            domino_cfg = os.path.join(install_dir, "config", "model", "sys", "domino.cfg")
            rtl_domino_cfg = os.path.join(install_dir, "config", "model", "sys", "domino-core-rtl.cfg")
            is_dir, is_base = os.path.split(os.path.join(args.release_dir, trace["file"]))
            is_stem = Path(Path(is_base).resolve().stem).stem
            sim_logfilename = os.path.join(is_dir, f"{is_stem}_regenerate_{datestring}.panthera.log")
            with open(sim_logfilename, "w") as sim_logfile:
                proc = subprocess.run(
                    [
                        panthera, "sim", "-v",
                        amperesys,
                        "-sys.soc_0.core_0.instruction_stream_name", is_path,
                        "-sys.soc_0.core_0.l2c.warmup_ms", l2c_path,
                        "-cfg", rtl_domino_cfg,
                        "-sys.soc_0.core_0.gpc.max_instructions", "100",
                        "-sys.soc_0.core_0.l2c.check_warmstart_overruns", "true",
                    ],
                    stderr=subprocess.STDOUT,
                    stdout=sim_logfile,
                )  # fmt: skip
                validated = proc.returncode == 0
        else:
            validated = True

        if validated:
            good_traces.append(trace)
        else:
            bad_traces.append(trace)

    bad_manifest_path = os.path.join(args.release_dir, "manifest.json.bad")
    if os.path.exists(bad_manifest_path):
        with open(bad_manifest_path) as fp:
            old_traces = json.load(fp)
            for t in old_traces:
                bad_traces.append(t)

    with open(os.path.join(args.release_dir, "manifest.json"), "w") as fp:
        json.dump(good_traces, fp, sort_keys=True, indent=4)

    with open(os.path.join(args.release_dir, "manifest.json.bad"), "w") as fp:
        json.dump(bad_traces, fp, sort_keys=True, indent=4)


if __name__ == "__main__":
    main()
