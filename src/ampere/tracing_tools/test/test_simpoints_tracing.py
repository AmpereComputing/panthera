# Copyright (c) 2021-2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

from panthera import install_dir
from panthera.tracing.bbv import BBV

panthera_py = os.path.realpath(os.path.join(install_dir, "bin", "panthera"))
is_validate = os.path.realpath(os.path.join(install_dir, "bin", "is_validate"))
is_viewer = os.path.realpath(os.path.join(install_dir, "bin", "is_viewer"))
bs_viewer = os.path.realpath(os.path.join(install_dir, "bin", "bs_viewer"))
ms_viewer = os.path.realpath(os.path.join(install_dir, "bin", "ms_viewer"))
is2bbv = os.path.realpath(os.path.join(install_dir, "bin", "is2bbv"))

EXEC_TMP = True
try:
    import psutil

    mnts = {part.mountpoint: part.opts for part in psutil.disk_partitions()}
    EXEC_TMP = "noexec" not in mnts.get(tempfile.gettempdir(), "")
except:
    pass


class TestSimpointsTracing(unittest.TestCase):
    def run_and_log(self, args, **kwargs):
        # Print command as it would appear if you typed it into shell (not as
        # quoted list of individual arguments)
        printable_args = args
        if not isinstance(args, (str, bytes)) and not isinstance(args, os.PathLike):
            printable_args = shlex.join(args)

        print(f"TestSimpointTracing: subprocess.run with command: {printable_args}")
        sys.stdout.flush()  # flush print() output so it appears before output of command being called by subprocess.run
        return subprocess.run(args, **kwargs)

    def get_is_bbv(self, is_filename, is_directory):
        result = self.run_and_log(
            [is2bbv, is_filename, "/dev/stdout"],
            check=True,
            cwd=is_directory,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        bbv = BBV()
        bbv.from_bbv_string(result.stdout.decode("utf-8"))
        return bbv

    def get_stream_stats(self, stream_viewer, stream_filename, is_directory):
        result = self.run_and_log(
            [stream_viewer, stream_filename],
            check=True,
            cwd=is_directory,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        pattern = re.compile(r"^([a-zA-Z\s]*[a-zA-Z])\s*=\s*([0-9]+)$")
        values = {}
        for line in result.stdout.decode("utf-8").splitlines():
            match = pattern.fullmatch(line)
            if match is not None:
                values[match.group(1)] = int(match.group(2))
        return values

    def check_dependent_add_micro_simpoints(self, tmpdirname):
        with open(os.path.join(tmpdirname, "dependent_adds.trace_intervals.json")) as trace_intervals_json_file:
            trace_intervals_json = json.loads(trace_intervals_json_file.read())
        with open(
            os.path.join(tmpdirname, "dependent_adds.generate_bbvs.generation_log.json")
        ) as bbv_generation_log_json_file:
            bbv_generation_log_json = json.loads(bbv_generation_log_json_file.read())

        # Ensure we traced about the right number of instructions in the BBV plugin
        self.assertAlmostEqual(bbv_generation_log_json["settings"]["total_instructions"], 102425978, delta=5000000)

        # Ensure traces were reported to be captured in the JSON file
        self.assertAlmostEqual(trace_intervals_json["total_trace_weight"], 1.0, delta=0.001)
        self.assertEqual(len(trace_intervals_json["traces"]), len(trace_intervals_json["simpoints"]))

        # Ensure weighted distances between collected intervals and original BBVs are minimal
        self.assertAlmostEqual(trace_intervals_json["weighted_summed_euclidean_distance"], 0.0, delta=0.006)
        self.assertAlmostEqual(trace_intervals_json["weighted_summed_manhattan_distance"], 0.0, delta=0.006)

        # Loop through all traces, ensuring streams for traces in JSON
        # exist, are valid, have at least a minimum number of expected
        # types of events, and also summing weighted BBVs for later
        # comparison.
        summed_weighted_bbvs = BBV()
        for trace in trace_intervals_json["traces"]:
            self.run_and_log([is_validate, trace["file"]], cwd=tmpdirname, check=True)
            bbv = self.get_is_bbv(trace["file"], tmpdirname) * float(trace["weight"])
            summed_weighted_bbvs += bbv

            is_stats = self.get_stream_stats(is_viewer, trace["file"], tmpdirname)
            self.assertGreaterEqual(is_stats["instruction count"], 10000)

            #                self.assertGreaterEqual(is_stats['memop count'], 500) # ignore for now, this is a dependent add benchmark
            self.assertGreaterEqual(is_stats["memory count"], 3)  # at least three PTEs (64k pages)
            self.assertGreaterEqual(is_stats["sysreg count"], 17)
            self.assertGreaterEqual(is_stats["context count"], 1)
            self.assertGreaterEqual(is_stats["control count"], 2)

            if trace["id"] != "0":
                bs_stats = self.get_stream_stats(bs_viewer, trace["branch_warmup_file"], tmpdirname)
                self.assertGreaterEqual(bs_stats["total branches"], 30)
                self.assertGreaterEqual(bs_stats["taken branches"], 30)
                self.assertGreaterEqual(bs_stats["total sysregs"], 17)
                self.assertGreaterEqual(bs_stats["total context events"], 1)

                ms_stats = self.get_stream_stats(ms_viewer, trace["pan_cache_preload_file"], tmpdirname)
                self.assertGreaterEqual(ms_stats["total access count"], 10)
                self.assertGreaterEqual(ms_stats["code count"], 10)
                self.assertGreaterEqual(ms_stats["bytes read count"], 100)
                # TODO check for data accesses for benchmarks that are not purely dependent adds...

        # Ensure distance between summed/weighted collected intervals and total summed is minimal
        original_bbv = BBV()
        with open(os.path.join(tmpdirname, trace_intervals_json["bbv_filename"])) as full_bbv_file:
            for line in full_bbv_file:
                b = BBV()
                b.from_bbv_string(line)
                original_bbv += b

        self.assertAlmostEqual(original_bbv.euclidean_distance(summed_weighted_bbvs), 0.0, delta=0.04)
        self.assertAlmostEqual(original_bbv.manhattan_distance(summed_weighted_bbvs), 0.0, delta=0.05)

        # TODO Can we check highest-weight trace to ensure it is right interval for this test somehow?

    def check_dependent_add_micro_snippets(self, tmpdirname):
        with open(
            os.path.join(tmpdirname, "dependent_adds_snippets.trace_intervals.json")
        ) as trace_intervals_json_file:
            trace_intervals_json = json.loads(trace_intervals_json_file.read())

        # Ensure traces were reported to be captured in the JSON file
        self.assertAlmostEqual(trace_intervals_json["total_trace_weight"], 0.9, delta=0.11)

        # We are attempting to collect 20 traces per simpoint interval, so even
        # if 3/4 of them fail, we should still have at least 5x more traces
        # than intervals
        self.assertGreaterEqual(len(trace_intervals_json["traces"]), 5 * len(trace_intervals_json["simpoints"]))

        # Ensure weighted distances between collected intervals and original BBVs are minimal
        self.assertAlmostEqual(trace_intervals_json["weighted_summed_euclidean_distance"], 0.1, delta=0.1)
        self.assertAlmostEqual(trace_intervals_json["weighted_summed_manhattan_distance"], 0.2, delta=0.2)

        # Loop through all traces, ensuring streams for traces in JSON
        # exist, are valid, have at least a minimum number of expected
        # types of events, and also summing weighted BBVs for later
        # comparison.
        summed_weighted_bbvs = BBV()
        sum_warmup_stats = {}
        num_warmup_traces = 0

        def record_stat(stat, number):
            sum_warmup_stats[stat] = number + sum_warmup_stats.get(stat, 0)

        for trace in trace_intervals_json["traces"]:
            self.run_and_log([is_validate, trace["file"]], cwd=tmpdirname, check=True)
            bbv = self.get_is_bbv(trace["file"], tmpdirname) * float(trace["weight"])
            summed_weighted_bbvs += bbv

            is_stats = self.get_stream_stats(is_viewer, trace["file"], tmpdirname)
            self.assertGreaterEqual(is_stats["instruction count"], 500)

            self.assertGreaterEqual(is_stats["memory count"], 3)  # at least three PTEs (64k pages)
            self.assertGreaterEqual(is_stats["sysreg count"], 17)
            self.assertGreaterEqual(is_stats["context count"], 1)
            self.assertGreaterEqual(is_stats["control count"], 2)

            self.assertTrue(os.path.exists(os.path.join(tmpdirname, trace["elf_file"])))
            # TODO can we run ELF files on QEMU to validate somehow (i.e.
            # collect BBV from ELF file and ensure it is close to that from
            # instruction stream)? IIRC, there is a conflict between AISLE and
            # QEMU memory mappings preventing this from working correctly at
            # the moment.

            if trace["id"] != "0":
                num_warmup_traces += 1
                bs_stats = self.get_stream_stats(bs_viewer, trace["branch_warmup_file"], tmpdirname)
                for s in ["total branches", "taken branches", "total sysregs", "total context events"]:
                    record_stat(s, bs_stats[s])

                ms_stats = self.get_stream_stats(ms_viewer, trace["pan_cache_preload_file"], tmpdirname)
                for s in ["total access count", "data count", "bytes read count", "code count"]:
                    record_stat(s, ms_stats[s])

        self.assertGreaterEqual(sum_warmup_stats["total branches"] / num_warmup_traces, 30)
        self.assertGreaterEqual(sum_warmup_stats["taken branches"] / num_warmup_traces, 30)
        self.assertGreaterEqual(sum_warmup_stats["total sysregs"] / num_warmup_traces, 17)
        self.assertGreaterEqual(sum_warmup_stats["total context events"] / num_warmup_traces, 1)
        self.assertGreaterEqual(sum_warmup_stats["total access count"] / num_warmup_traces, 20)
        self.assertGreaterEqual(sum_warmup_stats["code count"] / num_warmup_traces, 20)
        # TODO check for data accesses for benchmarks that are not purely dependent adds...
        self.assertGreaterEqual(sum_warmup_stats["bytes read count"] / num_warmup_traces, 100)

    @unittest.skipUnless(EXEC_TMP, "tmp is not executable")
    def test_tracing_dependent_add_micro(self):
        collateral_dir = os.environ["TEST_COLLATERAL_DIR"]
        if shutil.which("qemu-system-aarch64") is None or shutil.which("simpoint") is None:
            always_run = os.environ.get("ALWAYS_RUN_TRACING_INTEGRATION")
            if always_run is None or always_run.lower() == "false" or always_run.lower() == "f":
                print(
                    "Warning: Skipping tracing integration test because `qemu-system-aarch64` or `simpoint` were not found on $PATH. Set ALWAYS_RUN_TRACING_INTEGRATION=True to force test failure in this case"
                )
                return
            else:
                self.assertTrue(shutil.which("qemu-system-aarch64") != None)
                self.assertTrue(shutil.which("simpoint") != None)

        with tempfile.TemporaryDirectory() as tmpdirname:
            qcow_path = os.path.join(tmpdirname, "microbenchmarks.qcow2")
            kernel_path = os.path.join(collateral_dir, "Image-64k")

            # copy .qcow locally because the following commands create checkpoints on it
            shutil.copy(os.path.join(collateral_dir, "microbenchmarks.qcow2"), qcow_path)

            # The next three commands represent the "normal" simpoint flow:
            # 1. Collect BBVs, taking checkpoints during execution
            self.run_and_log(
                [
                    panthera_py, "tracing.generate_bbvs",
                    "--output", "dependent_adds",
                    "--rootfs", qcow_path,
                    "--kernel", kernel_path,
                    "--bbv-interval", "10000",
                    "--",
                    "bm_alu",
                    "--gtest_filter=LatALU.DependentAdds",
                ],
                cwd=tmpdirname,
                check=True,
            )  # fmt: skip
            # 2. Run simpoint tool on collected BBVs to identify representative intervals
            self.run_and_log(
                [panthera_py, "tracing.calculate_simpoints", "--input-json", "dependent_adds.generate_bbvs.json"],
                cwd=tmpdirname,
                check=True,
            )
            # 3. Trace identified simpoint intervals
            self.run_and_log(
                [
                    panthera_py, "tracing.trace_intervals",
                    "--input-json", "dependent_adds.calculate_simpoints.json",
                    "--start-online-warmup-after-offline-warmup-instructions", "5000",
                    "--start-measuring-after-online-warmup-instructions", "1000",
                ],
                cwd=tmpdirname,
                check=True,
            )  # fmt: skip

            self.check_dependent_add_micro_simpoints(tmpdirname)

            # Re-using the previous BBV collection (+ checkpoints) and simpoint
            # calculation steps to speed it up, modify the JSON file from
            # simpoint calculation to use a different output prefix (to avoid
            # filename collisions) and re-run `tracing.trace_intervals`, but
            # this time produce snippets instead of full simpoints.
            with open(
                os.path.join(tmpdirname, "dependent_adds.calculate_simpoints.json")
            ) as calculate_simpoints_json_file:
                calculate_simpoints_json = json.loads(calculate_simpoints_json_file.read())
            calculate_simpoints_json["bbv_args"]["output_prefix"] = "dependent_adds_snippets"
            with open(
                os.path.join(tmpdirname, "dependent_adds_snippets.calculate_simpoints.json"), "w"
            ) as calculate_simpoints_json_file:
                calculate_simpoints_json_file.write(json.dumps(calculate_simpoints_json, sort_keys=True, indent=4))
            self.run_and_log(
                [
                    panthera_py, "tracing.trace_intervals",
                    "--input-json", "dependent_adds_snippets.calculate_simpoints.json",
                    "--start-online-warmup-after-offline-warmup-instructions", "5000",
                    "--perfval-snippets",
                    "--trace-runlength", "500",
                    "--dcache-filter-size", "256",
                    "--icache-filter-size", "256",
                ],
                cwd=tmpdirname,
                check=False,
            )  # fmt: skip

            self.check_dependent_add_micro_snippets(tmpdirname)
