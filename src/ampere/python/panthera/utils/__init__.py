# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import json
import logging
import random
import socket
import tarfile
from collections import defaultdict
from os.path import expanduser
from pathlib import Path
from typing import Iterable, Union

import paramiko

try:
    import pyslurm
except:
    # pyslurm is not available in vl environment. The maintainer is not
    # supporting pypi. We also don't need this in vl setup since we don't have
    # slurm
    pass

logging.getLogger("paramiko").setLevel(logging.WARNING)


class Cluster:
    """This class abstracts the way we transfer files and directories
    between clusters. Currently, using ssh, we open a single connection and
    send connections via tar.  This greatly reduces the transfer speed for many
    small files"""

    def __init__(self, cluster: str, identity_file: str, base_path: str = "/", retry_cnt: int = 5):
        self.cluster = cluster
        self.retry_cnt = retry_cnt
        self.base_path = base_path
        self._ssh = paramiko.SSHClient()
        self._ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        self._key = paramiko.Ed25519Key.from_private_key_file(expanduser(identity_file))
        self._tar_fd = None
        self._channel = None

    def __enter__(self):
        self._connect()
        return self

    def __exit__(self, exc_type, exc, tb):
        self._close()
        return False  # Don't suppress exceptions

    def _setup_tar_connection(self):
        remote_cmd = f"mkdir -p {self.base_path} && tar -xzf - -C {self.base_path}"
        self._channel.exec_command(remote_cmd)
        self._tar_fd = tarfile.open(mode="w:gz", fileobj=self._channel.makefile("wb"))
        pass

    def _connect(self):
        nodes = Cluster.list()[self.cluster]
        nodes = random.sample(nodes, min(self.retry_cnt, len(nodes)))
        assert len(nodes) != 0, f"No available Host found on cluster {self.cluster}"

        # paramiko logs exceptions to error, we must disable
        # logging to keep log files clean while we retry.  Only
        # after we try retry
        log_level = logging.getLogger("paramiko").level
        logging.getLogger("paramiko").setLevel(logging.FATAL)
        connected = False

        for host in nodes:
            try:
                self._ssh.connect(hostname=host, pkey=self._key)
                self._channel = self._ssh.get_transport().open_session()
                logging.debug(f"Connected to host '{host}'")
                connected = True
                break
            except:
                logging.debug(f"Failed to connect to host '{host}'")

        logging.getLogger("paramiko").setLevel(log_level)
        if not connected:
            logging.error(f"Failed to connect to remote cluster nodes: {nodes}")

    def _close(self):
        if self._tar_fd:
            self._tar_fd.close()
            self._tar_fd = None

        self._channel.shutdown_write()
        return_code = self._channel.recv_exit_status()
        if return_code != 0:
            logging.error(f"Transfer failed rc = {return_code}")
        self._ssh.close()
        self._channel = None

    def delete(self, path) -> None:
        if Cluster.login_cluster() == self.cluster:
            logging.warning(f"Will Not remove {path} on login cluster")
        else:
            files = [path] if isinstance(path, str) else path
            for f in files:
                logging.debug(f"Removing '{f}' on {self.cluster}")
                cmd = f"rm -rf -- {f}"
                self._ssh.exec_command(cmd)

    def execute(self, exec) -> None:
        if Cluster.login_cluster() == self.cluster:
            logging.warning(f"Will Not run command {exec} on login cluster")
        else:
            logging.debug(f"Executing '{exec}' on {self.cluster}")
            self._ssh.exec_command(exec)

    def put(self, file: Union[str, Iterable[str]]) -> None:
        if self._tar_fd is None:
            self._setup_tar_connection()

        files = list(file) if isinstance(file, str) else file
        for f in files:
            if not Path(f).exists():
                raise FileNotFoundError(f)
            logging.debug(f"Sending file {f} to {self.cluster} cluster.")
            self._tar_fd.add(f)

    @classmethod
    def list(cls, partition: str | None = None) -> dict[str, list[str]]:
        """return available clusters and nodes within each cluster."""
        clusters = defaultdict(list)
        nodes = pyslurm.node().get()
        for _, info in nodes.items():
            hostname = info["node_hostname"]
            state = info["state"]
            partitions = info["partitions"]
            extra = "{}" if info.get("extra") is None else info.get("extra")
            allowed_states = {"IDLE": 0, "MIXED": 1, "ALLOCATED": 2}

            if state not in allowed_states:
                continue
            if partition is not None and partition not in partitions:
                continue

            extra = json.loads(extra)
            cluster = extra.get("cluster")
            clusters[cluster].append((hostname, state))

        # Sort machines in each cluster by idle
        for _, lst in clusters.items():
            lst.sort(key=lambda pair: allowed_states.get(pair[1], 99))

        clusters = {c: [hostname for hostname, _ in lst] for c, lst in clusters.items()}
        return clusters

    @classmethod
    def get_cluster(cls, node: str) -> str:
        for cluster, nodes in cls.list().items():
            if node in nodes:
                return cluster
        # Make best guess if not specified by slurm
        return "scc" if "scc" in node else "denver"

    @classmethod
    def my_cluster(cls) -> str:
        return Cluster.get_cluster(socket.getfqdn())

    @classmethod
    def login_cluster(cls) -> str:
        return "denver"

    @classmethod
    def compute_clusters(cls):
        clusters = cls.list()
        clusters.pop(cls.login_cluster())
        return list(clusters)
