# Copyright (c) 2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
from unittest import mock

from panthera.run_job import RunnableJob


class TestRunnableJobNixCache(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = TemporaryDirectory()
        self.cache_parent = Path(self.temporary_directory.name)
        self.job = RunnableJob(
            "job",
            ".",
            self.temporary_directory.name,
            cmds=[["/bin/true"]],
            meta={"panthera_root": "/panthera"},
        )

    def tearDown(self):
        self.temporary_directory.cleanup()

    def cache_home(self):
        return self.cache_parent / f"cheetah-nix-cache-{os.getuid()}"

    def test_cache_environment_uses_node_local_tmp(self):
        with mock.patch.object(RunnableJob, "NIX_CACHE_PARENT", self.cache_parent):
            env = self.job._nix_subprocess_env()

        self.assertEqual(env["XDG_CACHE_HOME"], str(self.cache_home()))
        self.assertTrue(self.cache_home().is_dir())
        self.assertEqual(self.cache_home().stat().st_mode & 0o777, 0o700)

    def test_run_passes_cache_environment_to_every_nix_invocation(self):
        with (
            mock.patch("panthera.run_job.install_using_nix", True),
            mock.patch("panthera.run_job.within_nix_env", return_value=False),
            mock.patch.object(RunnableJob, "NIX_CACHE_PARENT", self.cache_parent),
            mock.patch.object(self.job, "_verify_nix_env") as verify_nix_env,
            mock.patch("panthera.run_job.subprocess.run", return_value=SimpleNamespace(returncode=0)) as run,
        ):
            self.assertTrue(self.job.run())

        env = run.call_args.kwargs["env"]
        self.assertEqual(env["XDG_CACHE_HOME"], str(self.cache_home()))
        self.assertIs(verify_nix_env.call_args.kwargs["env"], env)

    def test_copy_passes_cache_environment_to_both_launchers(self):
        with (
            mock.patch("panthera.run_job.install_using_nix", True),
            mock.patch.object(RunnableJob, "NIX_CACHE_PARENT", self.cache_parent),
            mock.patch("panthera.run_job.subprocess.run", return_value=SimpleNamespace(returncode=0)) as run,
        ):
            self.job.copy("job.json")

        self.assertEqual(run.call_count, 2)
        for call in run.call_args_list:
            self.assertEqual(call.kwargs["env"]["XDG_CACHE_HOME"], str(self.cache_home()))
