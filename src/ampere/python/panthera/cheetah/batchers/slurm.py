#!/usr/bin/env python3
# Copyright (c) 2025-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


import logging
import os
import shlex
import subprocess
import sys
import warnings
from argparse import ArgumentParser
from textwrap import dedent

import numpy as np
from sklearn.cluster import KMeans
from sklearn.exceptions import ConvergenceWarning

from ..batch import Batch
from ..job import Job


class Slurm(Batch):
    MAX_RETRY = 3
    EXPORT_ENV = "HOME,USER,PWD,LANG,LANGUAGE"

    def __init__(self, study_name: str, study_directory: str, jobs: list[Job], **kwargs):
        super().__init__(study_name, study_directory, jobs, **kwargs)

        # Note: This is helpful when running in environments for which we cannot
        # expect the PATH to be properly setup prior to executing this script (i.e.
        # running on slurm, where the environment has been at least partially scrubbed)
        self.use_login_shell = True

        self.slurm_opt = getattr(self.args, "slurm_opt", "")
        self.slurm_account = getattr(self.args, "slurm_account", None)
        self.slurm_partition = getattr(self.args, "slurm_partition", None)
        self.slurm_qos = getattr(self.args, "slurm_qos", None)
        self.slurm_array_count = getattr(self.args, "slurm_array_count", 5)
        self.slurm_memory_scale = getattr(self.args, "slurm_memory_scale", 1.25)
        self.slurm_default_mem_max = getattr(self.args, "slurm_default_mem", 1536)

        self.slurm_timeout = getattr(self.args, "slurm_timeout", None)

        self.clusters = self.cluster_jobs(k=self.slurm_array_count, memory_scale=self.slurm_memory_scale)

    @classmethod
    def parser_add_args(cls, parser: ArgumentParser):
        slurm_group = parser.add_argument_group("Slurm options")

        slurm_group.add_argument("--slurm-account", help="Slurm account")
        slurm_group.add_argument("--slurm-partition", help="Slurm partition")
        slurm_group.add_argument("--slurm-qos", "-q", help="Slurm QoS")
        slurm_group.add_argument("--slurm-opt", default="", help="Override sbatch options")
        slurm_group.add_argument(
            "--slurm-array-count", default=5, type=int, help="Total number of job arrays to submit to the cluster"
        )
        slurm_group.add_argument(
            "--slurm-memory-scale",
            default=1.25,
            type=float,
            help="Scale memory usage number of number specified in trace by factor",
        )
        slurm_group.add_argument(
            "--slurm-default-mem",
            default=1536,
            type=int,
            help="Default max memory in MB used when not specified in trace (doesn't get scaled by --slurm-memory-scale)",
        )
        slurm_group.add_argument("--slurm-timeout", type=int, help="Slurm job timeout (minutes)")

    def slurm_filename(self, cluster):
        return self.work_path(self.study_file(f"_{cluster}.sbatch"))

    def slurm_jobs_txt(self, cluster):
        return os.path.join(self.study_directory, f"jobs_{cluster}.txt")

    @property
    def run_file(self):
        cmd = super().run_file
        return f"/bin/bash -l {cmd}" if self.use_login_shell else cmd

    def write_sbatch_file(self, cluster_num, max_mem, job_count) -> str:
        slurm_filename = self.slurm_filename(cluster_num)
        slurm_args = {
            "job-name": self.study_name,
            "requeue": None,
            "chdir": self.workdir,
            "output": "/dev/null",
            "error": "/dev/null",
            "array": f"1-{job_count}",
            "mem": max_mem,
        }
        for name, value in (
            ("account", self.slurm_account),
            ("partition", self.slurm_partition),
            ("qos", self.slurm_qos),
        ):
            if value:
                slurm_args[name] = value
        if self.slurm_timeout is not None and self.slurm_timeout > 0:
            slurm_args["time"] = self.slurm_timeout

        with open(slurm_filename, "w") as f:
            f.write("#!/bin/bash\n")
            for arg, value in slurm_args.items():
                f.write(f"#SBATCH --{arg}")
                if value is not None:
                    f.write(f"={value}")
                f.write("\n")

            f.write(
                dedent(f"""
                LINE=$SLURM_ARRAY_TASK_ID
                JOB="$(sed -n ${{LINE}}p "{self.slurm_jobs_txt(cluster_num)}")"
                JOB_BASE="${{JOB%.*}}"

                # Redirect to log file
                exec > "${{JOB_BASE}}.${{SLURM_ARRAY_JOB_ID}}_${{SLURM_ARRAY_TASK_ID}}.slurm.log" 2>&1

                echo "==================== SLURM Environment ===================="
                env | grep SLURM
                echo -e "===========================================================\\n"

                if [[ $SLURM_RESTART_COUNT -ge {self.MAX_RETRY} ]]; then
                    echo "ERROR: max retry count hit ${{SLURM_RESTART_COUNT}} attempts." | tee >(cat >&2)
                    exit 10 # Exit and don't restart
                else
                    [[ ! -z $SLURM_RESTART_COUNT ]] && echo "WARN: retrying, attempt ${{SLURM_RESTART_COUNT}}." | tee >(cat >&2)
                fi

                {self.run_file} "{self.work_path("$JOB")}"
            """).strip("\n")
            )
        os.chmod(slurm_filename, 0o755)
        return slurm_filename

    def cluster_jobs(self, k=3, memory_scale=1.25):
        # K-means clustering is probably not the best heuristic for deciding
        # how to cluster these jobs (though its definitely not the worst!). It
        # may help improve slurm throughput/efficiency to use an algorithm that
        # better minimizes the amount of over-requested memory.
        warnings.filterwarnings("ignore", category=ConvergenceWarning)

        memory_vals = np.array(
            [
                [round(job.meta["max_resident_mb"]["amperesys"] * memory_scale)]
                if ("max_resident_mb" in job.meta and "amperesys" in job.meta["max_resident_mb"])
                else [self.slurm_default_mem_max]
                for job in self.jobs
            ]
        )
        cluster_cnt = min(k, len(self.jobs))  # We can't create more arrays than jobs
        kmeans = KMeans(n_clusters=cluster_cnt)
        kmeans.fit(memory_vals)
        labels = kmeans.labels_
        actual_k = len(np.unique(labels))

        # Get the highest number in each cluster
        clusters = {}
        for cluster in range(actual_k):
            cluster_points = memory_vals[labels == cluster]
            max_rss = np.max(cluster_points)
            clusters[cluster] = {"max_rss": max_rss, "jobs": list()}

        # Assign jobs to cluster
        for i, job in enumerate(self.jobs):
            cluster = clusters[labels[i]]
            cluster["jobs"].append(job)

        # Sort clusters by memory
        return {index: cluster for index, cluster in enumerate(sorted(clusters.values(), key=lambda c: c["max_rss"]))}

    def setup(self) -> None:
        logging.info(f"Creating {len(self.clusters)} jobs")

        # Iterate through each cluster of jobs, a cluster here is grouped by memory usage.
        for num, jobs_cluster in self.clusters.items():
            jobs = jobs_cluster["jobs"]
            logging.info(f" ...{len(jobs)} traces with memory limit of {jobs_cluster['max_rss']} MiB.")
            job_txt_file = self.create_job_file(name=self.slurm_jobs_txt(num), jobs=jobs)
            sbatch_file = self.write_sbatch_file(cluster_num=num, max_mem=jobs_cluster["max_rss"], job_count=len(jobs))
            logging.debug("Created %s and %s", job_txt_file, sbatch_file)

    def run(self) -> bool | None:
        # Submit sbatch job
        sbatch_jobs = []
        jobs = []
        for num in self.clusters:
            args = ["sbatch", f"--export={self.EXPORT_ENV}", "--parsable", "--wait"]
            args += shlex.split(self.slurm_opt)
            args += [self.slurm_filename(num)]

            sbatch_proc = subprocess.Popen(args, stdout=subprocess.PIPE)
            sbatch_jobid = sbatch_proc.stdout.readline().decode().strip()
            sbatch_rc = sbatch_proc.poll()

            if sbatch_rc is not None:
                # Kill previous jobs that did start
                subprocess.run(["scancel", f"{' '.join(jobs)}"])
                raise Exception(
                    f"sbatch command failed with return code:{sbatch_proc.returncode}, cmd:{sbatch_proc.args}"
                )
            jobs.append(sbatch_jobid)
            sbatch_jobs.append(sbatch_proc)

        logging.info(f"Submitted batch job(s) {jobs}")
        p_status = None
        if sys.stdout.isatty():
            status_cmd = os.path.join(self.panthera_install, "lib/python/panthera/cheetah/slurm_status.sh")
            p_status = subprocess.Popen(["bash", status_cmd, f"{','.join(jobs)}", f"{self.job_count}"])

        try:
            for sbatch_job in sbatch_jobs:
                sbatch_job.wait()
        except KeyboardInterrupt:
            logging.info(f"Canceling batch job(s) {jobs}...")
            subprocess.run(["scancel"] + jobs)
            logging.info("Done.")
        except Exception as e:
            logging.warning(f"Study with jobs {jobs} is still running.")
            raise e
        finally:
            if p_status is not None:
                p_status.kill()
                print()

        return None
