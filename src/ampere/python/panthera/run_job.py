#!/usr/bin/env python3
# Copyright (c) 2024-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


# NOTE: nothing in this file should depend *inner* modules (i.e. cheetah)
# everything here must use python3 standard library and it's difficult to require
# that with module import rules

import argparse
import fcntl
import hashlib
import json
import logging
import os
import platform
import re
import shlex
import subprocess
from datetime import datetime, timedelta
from glob import glob
from pathlib import Path
from typing import Any

from . import install_dir, install_using_nix, nix_shell, setup_logging, within_nix_env
from .constants import SUMMARY_CSV_SUFFIX, TIMESERIES_CSV_SUFFIX


class RunnableJob:
    """A subset of a job which is specifically only for running."""

    NIX_CACHE_PARENT = Path("/tmp")

    FILE_SUFFIXES = {
        "log": ".log",
        "summary": f".{SUMMARY_CSV_SUFFIX}",
        "timeseries": f".{TIMESERIES_CSV_SUFFIX}",
        "jobfile": ".json",
    }

    def __init__(
        self,
        name,
        directory,
        study_directory,
        cmds=None,
        meta=None,
        cmds_meta=None,
    ):
        self.name = name
        self.cmds = cmds or []
        self.meta = meta or {}
        self.cmds_meta = cmds_meta or [{} for _ in self.cmds]
        self.directory = directory
        self.study_directory = os.path.abspath(study_directory)

    @classmethod
    def from_json(
        cls,
        job: dict[str, Any],
        study_directory: str,
        job_directory: str | None = None,
    ):
        cmds = job["cmds"]
        meta = job.get("meta", {})
        cmds_meta = job.get("cmds_meta")
        if cmds_meta is None:
            cmds_meta = [meta] * len(cmds) if cmds else None
        else:
            cmds_meta = [{**meta, **cmd_meta} for cmd_meta in cmds_meta]

        directory = job_directory or os.path.normpath(job["directory"])
        return cls(
            job["name"],
            directory,
            study_directory,
            cmds=cmds,
            meta=meta,
            cmds_meta=cmds_meta,
        )

    @classmethod
    def from_json_file(cls, jobfile: str, study_directory: str | None = None):
        with open(jobfile, "r") as jf:
            job = json.load(jf)

        directory = os.path.normpath(job["directory"])
        jobfile_directory = Path(jobfile).absolute().parent
        if study_directory is None:
            study_directory_parts = jobfile_directory.parts
            for i in range(len(study_directory_parts) + 1):
                study_file = Path(*study_directory_parts[:i], "study.json")
                if study_file.exists():
                    study_directory = study_file.parent
                    break

            if study_directory is None:
                raise RuntimeError("Cannot find study directory")

        return cls.from_json(
            job,
            study_directory=study_directory,
            job_directory=directory,
        )

    @property
    def absolute_directory(self):
        return os.path.join(self.study_directory, self.directory)

    @property
    def log(self):
        return f"{self.name}{self.FILE_SUFFIXES['log']}"

    def make_filename(self, suffix=None, name=None, absolute=False):
        """Construct a file name based on the directory and name of the job"""
        return os.path.join(
            self.absolute_directory if absolute else self.directory,
            name or (self.name + suffix),
        )

    def __getattr__(self, name):
        """Accessor for file paths in the form of <name>_<relpath|abspath>.

        "relpath" is relative to the study and "abspath" is an absolute path.
        """
        try:
            file, path_type = name.split("_")
            if path_type not in ("relpath", "abspath"):
                raise AttributeError(name)

            return self.make_filename(self.FILE_SUFFIXES[file], absolute=path_type == "abspath")
        except (ValueError, KeyError):
            raise AttributeError(name)

    def nix_best_effort_hash(self):
        """
        Do our best to generate a hash which is likely to be unique to a
        particular Nix setup. We want this hash to be unique so we can assume
        that if a file with this filename exists, we probably don't have to
        flock/serialize ourselves to ensure we don't have lots of threads Nix
        all trying to download/build the same environment simultaneously.
        """
        # First, collect a list of all the *.nix files we want to hash
        files = []
        home_nix = os.path.expanduser("~/.panthera.nix")
        if os.path.exists(home_nix):
            files.append(home_nix)

        # Find files named *.nix immediately contained in the install directory
        files.extend([os.path.realpath(filename) for filename in glob(os.path.join(install_dir, "*.nix"))])

        # Find files named *.nix recursively inside the install_dir/nix
        # directory
        files.extend(
            [os.path.realpath(filename) for filename in glob(os.path.join(install_dir, "**", "*.nix"), recursive=True)]
        )

        # Once we have identified all the files which are _likely_ to impact
        # nix, hash them all together
        hasher = hashlib.new("md5")
        for filename in files:
            hasher.update(bytes(f"\n=-=-=-={filename}=-=-=-=\n", "utf-8"))
            with open(filename, "rb") as file:
                while chunk := file.read(4192):
                    hasher.update(chunk)

        return hasher.hexdigest()

    def _nix_subprocess_env(self):
        """Return an environment that puts Nix's cache on this node."""
        cache_home = self.NIX_CACHE_PARENT / f"cheetah-nix-cache-{os.getuid()}"
        cache_home.mkdir(mode=0o700, exist_ok=True)
        return {**os.environ, "XDG_CACHE_HOME": str(cache_home)}

    def nix_env_setup(self, cmd, env=None):
        """
        Running nix-shell once at a time per machine prevents overloading the
        nix cache server
        """
        save_umask = os.umask(0)

        nix_lock_directory = Path("/tmp/cheetah_nix_locks")
        os.makedirs(nix_lock_directory, mode=0o774, exist_ok=True)

        hostname = platform.node()
        nix_hash = self.nix_best_effort_hash()
        lock_filename = nix_lock_directory / f"nix_env_setup.{hostname}.{nix_hash}.lock"
        done_filename = nix_lock_directory / f"nix_env_setup.{hostname}.{nix_hash}.done"
        datetime_format = "%a %d %b %Y, %I:%M%p"

        def contains_recent_timestamp(filename):
            if not filename.exists():
                return False
            with open(filename, "rt") as file:
                timestamp = datetime.strptime(file.read().strip(), datetime_format)
                return timestamp + timedelta(hours=24) > datetime.now()

        def opener(path, flags):
            return os.open(path, flags, 0o774)

        with open(lock_filename, "w", opener=opener) as lock_file:
            status = None
            fcntl.flock(lock_file, fcntl.LOCK_EX)
            try:
                # If another process on this machine has already run nix-shell
                # for this hash in the last 24 hours, we can avoid running it
                # again
                if contains_recent_timestamp(done_filename):
                    return 0  # success

                status = subprocess.run(cmd, close_fds=False, env=env)

                # If we successfully ran nix-shell, write the current timestamp
                # to the 'done' file to let others know it has been run (and
                # when)
                if status.returncode == 0:
                    with open(done_filename, "wt", opener=opener) as done_file:
                        done_file.write(datetime.now().strftime(datetime_format))
            finally:
                fcntl.flock(lock_file, fcntl.LOCK_UN)
                os.umask(save_umask)

            return status.returncode if status is not None else -1

    def _nixify_run_cmd(self, cmd, shell=False):
        nix_cmd = ["/nix/var/nix/profiles/default/bin/nix-shell", "--pure", "--run"]
        if shell:
            return " ".join(nix_cmd + [shlex.quote(cmd), nix_shell])
        return nix_cmd + [shlex.join(cmd)] + [nix_shell]

    def _verify_nix_env(self, env=None):
        """Verify nix environment before running."""
        cmd = self._nixify_run_cmd(["/bin/true"])
        status = self.nix_env_setup(cmd, env=env)
        if status != 0:
            raise ValueError("Nix environment setup error {} exited with {} status\n".format(shlex.join(cmd), status))

    def _run_cmd(self, cmd, shell=False):
        """Format job command for running and printing."""
        if not shell:
            if install_using_nix and not within_nix_env():
                cmd = self._nixify_run_cmd(cmd)
            return cmd, shlex.join(cmd)

        redirection = re.compile(r"^\d?[|&]?[<>]+[|&]?\w*$")
        cmdline = cmd = " ".join(c if redirection.match(c) else shlex.quote(c) for c in cmd)
        if install_using_nix and not within_nix_env():
            cmdline = cmd = self._nixify_run_cmd(cmd, shell=True)
        return cmd, cmdline

    def get_meta(self, name, cmd_meta=None, default=None):
        global_meta_value = self.meta.get(name, default)
        if cmd_meta:
            return cmd_meta.get(name, global_meta_value)
        return global_meta_value

    def run(self, chdir=False, shell=None):
        """Run the commands in the Job."""
        nix_env = self._nix_subprocess_env() if install_using_nix and not within_nix_env() else None

        cwd = os.getcwd()
        if chdir:
            os.chdir(self.absolute_directory)
            log = self.log
        else:
            log = self.log_abspath

        logging.debug(f"Running in {os.getcwd()} directory")

        all_successful = True
        with open(log, "w") as logfile:
            if install_using_nix and not within_nix_env():
                try:
                    self._verify_nix_env(env=nix_env)
                except ValueError as e:
                    logfile.write(str(e))
                    return False

            for cmd, cmd_meta in zip(self.cmds, self.cmds_meta):
                cmd_in_shell = self.get_meta("run_in_shell", cmd_meta, False) if shell is None else shell
                cmd, cmdline = self._run_cmd(cmd, shell=cmd_in_shell)

                logfile.write(cmdline + "\n")
                logfile.flush()

                status = subprocess.run(cmd, stdout=logfile, stderr=logfile, shell=cmd_in_shell, env=nix_env)
                if status.returncode == 0:
                    logfile.write("Command successful exit.\n")
                else:
                    all_successful = False
                    logfile.write("Command error {} exited with {} status\n".format(cmdline, status.returncode))
                    break

            if all_successful:
                logfile.write("All commands exited successfully.\n")

        if chdir:
            os.chdir(cwd)

        return all_successful

    def copy(self, job_file, chdir=False):
        # The installed panthera launcher may enter nix-shell independently of
        # run(), so pass its subprocesses their own Nix cache environment.
        nix_env = self._nix_subprocess_env() if install_using_nix else None

        cwd = os.getcwd()
        if chdir:
            os.chdir(self.absolute_directory)
            log = self.log
        else:
            log = self.log_abspath

        panthera_root = self.get_meta("panthera_root")

        with open(log, "a") as logfile:
            cmd = [f"{panthera_root}/bin/panthera", "cheetah.cp", "--push", "--job", job_file]
            status = subprocess.run(cmd, stdout=logfile, stderr=logfile, env=nix_env)
            if status.returncode == 0:
                logfile.write("All outputs copied back successfully\n")
            else:
                logfile.write("Copyback error exited with {} status\n".format(status.returncode))

        log_files = [log]
        slurm_log = f"{self.name}.slurm.log"
        if Path(slurm_log).exists():
            log_files.append(slurm_log)
        cmd = [f"{panthera_root}/bin/panthera", "cheetah.cp", "--push"]
        for p in log_files:
            cmd.extend(["--file", p])
        status = subprocess.run(cmd, check=True, env=nix_env)

        if chdir:
            os.chdir(cwd)


def parse_args():
    parser = argparse.ArgumentParser(description="Run cheetah job from JSON")
    parser.add_argument(
        "--chdir",
        action="store_true",
        default=False,
        help="change to job directory before running",
    )
    parser.add_argument(
        "--copy",
        action="store_true",
        default=False,
        help="Attempt to copy files back to login cluster",
    )
    parser.add_argument(
        "--study-directory",
        help="explicit study directory",
    )
    parser.add_argument(
        "--require-outside-nix",
        action="store_true",
        help="Require being run outside of a Nix environment",
    )
    parser.add_argument(
        "--debug",
        action="store_true",
        help="Enable debug logging",
    )
    parser.add_argument("job", help="job specification (JSON) file")
    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()
    setup_logging(debug=args.debug)

    if args.require_outside_nix and within_nix_env():
        raise ValueError("Within a nix environment")

    job = RunnableJob.from_json_file(args.job, study_directory=args.study_directory)
    job.run(chdir=args.chdir)
    if args.copy:
        job.copy(chdir=args.chdir, job_file=args.job)
