# Copyright (c) 2023-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import platform
import re
import shlex
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest

from panthera import install_dir

panthera_py = os.path.realpath(os.path.join(install_dir, "bin", "panthera"))
is_validate = os.path.realpath(os.path.join(install_dir, "bin", "is_validate"))
is_info = os.path.realpath(os.path.join(install_dir, "bin", "is_info"))

EXEC_TMP = True
try:
    import psutil

    mnts = {part.mountpoint: part.opts for part in psutil.disk_partitions()}
    EXEC_TMP = "noexec" not in mnts.get(tempfile.gettempdir(), "")
except:
    pass


class TestMicroTracing(unittest.TestCase):
    @classmethod
    def printable_args(cls, args):
        printable_args = args
        if not isinstance(args, (str, bytes)) and not isinstance(args, os.PathLike):
            printable_args = shlex.join(args)
        return printable_args

    @classmethod
    def run_and_log(cls, args, capture_output=False, **kwargs):
        # Print command as it would appear if you typed it into shell (not as
        # quoted list of individual arguments)
        printable_args = cls.printable_args(args)
        print(f"TestSimpointTracing: subprocess.run with command: {printable_args}")
        sys.stdout.flush()  # flush print() output so it appears before output of command being called by subprocess.run

        if capture_output:
            return subprocess.run(args, stdout=subprocess.PIPE, **kwargs)
        return subprocess.run(args, **kwargs)

    @classmethod
    def get_stream_stats(cls, stream_viewer, stream_filename, is_directory):
        result = cls.run_and_log([stream_viewer, stream_filename], check=True, cwd=is_directory, stdout=subprocess.PIPE)
        pattern = re.compile(r"^([a-zA-Z\s]*[a-zA-Z])\s*=\s*([0-9]+)$")
        values = {}
        for line in result.stdout.decode("utf-8").splitlines():
            match = pattern.fullmatch(line)
            if match is not None:
                values[match.group(1)] = int(match.group(2))
        return values

    @classmethod
    def get_trigger_breakdown_stats(cls, stream_filename, is_directory):
        result = cls.run_and_log([is_info, stream_filename], check=True, cwd=is_directory, stdout=subprocess.PIPE)
        pattern = re.compile(
            r"^\s*\(\s*([0-9]+)\s*warmup,\s*([0-9]+)\s*measurement,\s*and\s*([0-9]+)\s*cooldown\s*\)\s*$"
        )
        values = {}
        for line in result.stdout.decode("utf-8").splitlines():
            match = pattern.fullmatch(line)
            if match is not None:
                values["warmup"] = int(match.group(1))
                values["measurement"] = int(match.group(2))
                values["cooldown"] = int(match.group(3))
        return values

    @staticmethod
    def trace_elf():
        return platform.machine().startswith("x86") and shutil.which("module") is not None

    @staticmethod
    def run_tracing(cwd, output, stats_type, cmd):
        elf_tracing = TestMicroTracing.trace_elf()
        results = {}
        results["trace"] = TestMicroTracing.run_and_log(
            [panthera_py, "tracing.trace_micro"] + cmd + (["--trace-elf"] if elf_tracing else []),
            cwd=cwd,
            check=True,
            capture_output=True,
        )
        results["validate"] = TestMicroTracing.run_and_log(
            [is_validate, output], cwd=cwd, check=True, capture_output=True
        )
        if stats_type == "stream":
            results["stats"] = TestMicroTracing.get_stream_stats(is_info, output, cwd)
        else:
            results["stats"] = TestMicroTracing.get_trigger_breakdown_stats(output, cwd)

        return results

    def setUp(self):
        collateral_dir = os.environ["TEST_COLLATERAL_DIR"]
        if shutil.which("qemu-system-aarch64") is None:
            always_run = os.environ.get("ALWAYS_RUN_TRACING_INTEGRATION")
            if always_run is None or always_run.lower() == "false" or always_run.lower() == "f":
                print(
                    "Warning: Skipping micro tracing integration test because `qemu-system-aarch64` was not found on $PATH. Set ALWAYS_RUN_TRACING_INTEGRATION=True to force test failure in this case"
                )
                return
            else:
                self.assertIsNotNone(shutil.which("qemu-system-aarch64"))

        self.working_dir = tempfile.TemporaryDirectory()

        tmp_micro_path = os.path.join(self.working_dir.name, self.micro_name)
        shutil.copy(os.path.join(collateral_dir, self.micro_name), tmp_micro_path)
        os.chmod(
            tmp_micro_path,
            stat.S_IXUSR | stat.S_IRUSR | stat.S_IWUSR | stat.S_IXGRP | stat.S_IRGRP | stat.S_IXOTH | stat.S_IROTH,
        )

        self.micro_path = tmp_micro_path
        self.kernel_path = os.path.join(collateral_dir, "Image-64k")

    def tearDown(self):
        self.working_dir.cleanup()

    @property
    def output_files(self):
        return [
            path
            for path in os.listdir(self.working_dir.name)
            if not path.endswith(".tmp") and not os.path.basename(path).startswith(".coverage")
        ]

    def run_micro(self):
        cmd = self.command(self.micro_path, self.kernel_path)

        # Generate traces from microbenchmarks and verify
        results = TestMicroTracing.run_tracing(self.working_dir.name, self.stream_name, self.stats_type, cmd)

        # Print outputs
        printable_args = self.printable_args(cmd[-1])
        print(f"TestSimpointTracing: subprocess.run with command: {printable_args}")
        for name, output in results.items():
            if not isinstance(output, dict) and output.stdout is not None:
                print(f"{name} output:")
                print(output.stdout.decode("utf-8"))

        self.check_micro_trace_output("checkpoint" in results)
        self.check_micro_trace_stats(results["stats"])
