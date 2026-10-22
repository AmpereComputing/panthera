#! /bin/bash
# Copyright (c) 2023-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


if [ "$1" == "" ] || [ "$1" == "-h" ] || [ "$1" == "--help" ]; then
	echo "usage: $0 <drift/study dir> <panthera root directory> [panthera tool directory] [ specification name/path ]"
	echo
	echo "Run a drift study and rollup for a Panthera directory."
	echo "Set DRIFT_TLIST to the workload manifest to run."
	exit
fi

STUDY_NAME=results
REGRESS_DIR=$(realpath $(dirname "${BASH_SOURCE[0]}"))
DRIFT_DIR="$1"
PANTHERA_DIR="$2"
if [ "$3" == "" ]; then
	PANTHERA_TOOL_DIR="$PANTHERA_DIR"
else
	PANTHERA_TOOL_DIR="$3"
fi
if [ "$4" == "" ]; then
    DRIFT_SPEC="$REGRESS_DIR"/drift_specifications/latest.json
elif [ -f "$4" ]; then
    DRIFT_SPEC="$4"
else
    DRIFT_SPEC="$REGRESS_DIR/drift_specifications/${4}.json"
fi

if [ ! -f "$DRIFT_SPEC" ]; then
    echo "No drift specification: $DRIFT_SPEC"
    exit 1
fi
DRIFT_SPEC=$(realpath "$DRIFT_SPEC")
if [ -z "${DRIFT_TLIST:-}" ]; then
    echo "Set DRIFT_TLIST to a workload manifest"
    exit 1
fi
if [ ! -f "$DRIFT_TLIST" ]; then
    echo "No drift tracelist: $DRIFT_TLIST"
    exit 1
fi
DRIFT_TLIST=$(realpath "$DRIFT_TLIST")

pushd "$DRIFT_DIR" > /dev/null

md5sum "$DRIFT_SPEC" > specification.txt
md5sum "$DRIFT_TLIST" > tracelist.txt

PARTITION=short
TRACES=$(grep '"name"' "$DRIFT_TLIST" | wc -l)
EXPERIMENTS=$(grep '"name"' "$DRIFT_SPEC" | wc -l)
TOTAL_JOBS=$(( $TRACES * $EXPERIMENTS ))
if [ $TOTAL_JOBS -gt 1000 ]; then
	PARTITION=normal
fi

"$PANTHERA_TOOL_DIR"/install/bin/panthera cheetah \
    --slurm-qos $PARTITION \
    --specification "$DRIFT_SPEC" \
    --workloads "$DRIFT_TLIST" \
    --name $STUDY_NAME \
    --panthera-root "$PANTHERA_DIR/install" \
    --slurm-default-mem 2048

"$PANTHERA_TOOL_DIR"/install/bin/panthera cheetah.errors \
    --study $STUDY_NAME/study.json \
    --format list > $STUDY_NAME.errors

"$PANTHERA_TOOL_DIR"/install/bin/panthera rollup \
    $STUDY_NAME/study.json \
    --formula "$PANTHERA_TOOL_DIR"/install/config/rollup/rollup_formula.py \
    --output $STUDY_NAME.csv > /dev/null 2>&1

popd > /dev/null
