#! /bin/bash
# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


set -e

EXAMPLE_FOLDER=$(realpath "$1")

pushd "$EXAMPLE_FOLDER" > /dev/null

# Clean
rm -f init *.csv *.bbv *.qcow2 *.log *.json
rm -rf rtl_traces.* is_traces.* junk.*

# Run
for script in "$EXAMPLE_FOLDER/"*.sh; do
    echo "Running $(basename $script)..."
    $script
done

popd > /dev/null
