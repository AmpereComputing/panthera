#! /bin/bash
# Copyright (c) 2023-2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


if [ "$1" == "" ] || [ "$1" == "-h" ] || [ "$1" == "--help" ]; then
	echo "usage: $0 <repo src> <install dest> [<ref>]"
	echo
	echo "Build a specific reference from an existing repo src."
	echo "By default it will build the latest point where your"
	echo "tree diverged from main."
	exit
fi

SRC="$(realpath $1)"
DEST="$(realpath $2)"
REPO=$(mktemp -d)

pushd "$SRC" > /dev/null

if [ "$3" == "" ]; then
	REF=$(git merge-base HEAD origin/main)
else
	REF="$3"
fi

trap "pushd $SRC > /dev/null ; git worktree remove -f $REPO ; popd > /dev/null" EXIT

echo "Building $REF..."
git worktree add $REPO $REF > /dev/null

pushd $REPO > /dev/null

mkdir -p $DEST
make install > $DEST/build.log 2>&1

popd > /dev/null

popd > /dev/null

mv $REPO/install $DEST/install
