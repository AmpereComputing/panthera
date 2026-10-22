# Copyright (c) 2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import subprocess
from os.path import join

from .. import install_dir

EVP = join(install_dir, "bin", "evp")


def run(es_file, args, capture_output=False, check=True):
    process_args = {"check": check}
    if capture_output:
        process_args["stdout"] = subprocess.PIPE
    return subprocess.run([EVP, "--es", es_file] + args, **process_args)
