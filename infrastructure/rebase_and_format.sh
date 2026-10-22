#!/bin/bash
# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


if [[ "$1" == "-h" || "$1" == "--help" ]]; then
    echo "usage $0: [-l|--local] [UPSTREAM_BRANCH] [COMMIT...]"
    echo
    echo "Rebase and format current branch with upstream, incorporating formatting commits."
    echo
    echo "Options:"
    echo "  -l, --local      Format only local commits"
    echo "  -c, --changed    Changed files only"
    echo "  UPSTREAM_BRANCH  Upstream branch to use (default is origin/main)"
    echo "  COMMITS          Formatting commits to incorporate (default is known set)"
    exit 1
fi

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

LOCAL=
if [[ "$1" == "-l" || "$1" == "--local" ]]; then
    LOCAL=1
    shift
fi

CHANGED=
if [[ "$1" == "-c" || "$1" == "--changed" ]]; then
    CHANGED=1
    shift
fi

if [[ -n "$1" ]]; then
    UPSTREAM_BRANCH="$1"
    shift
else
    UPSTREAM_BRANCH="origin/main"
fi

if [[ ! -f Makefile || ! -f shell.nix ]]; then
    echo "Should be run in repository root directory!"
    exit 1
fi

if ! git rebase --show-current-patch &> /dev/null ; then
    if ! git diff HEAD --quiet; then
        echo "Must execute in a clean tree!"
        exit 1
    fi
fi

STARTING_COMMIT=$(git rev-parse HEAD)
echo "Commit before formatting: $STARTING_COMMIT"

if [[ -n "$LOCAL" ]]; then
    # Format only local commits
    if [[ "$#" != "0" ]]; then
        echo "Formatting commits are unsupported in --local!"
        exit 1
    fi

    echo "Rebase local commits..."
    commit=$(git merge-base HEAD "$UPSTREAM_BRANCH")
    if ! git rebase -X theirs -x "$SCRIPT_DIR/format_and_amend.sh" "$commit"; then
        echo "Error formatting, must fix manually before continuing!"
        exit 1
    fi
    exit 0
fi

DEFAULT_FORMAT_COMMITS=(8eb2f3e7d2f11b21151c080cc670fbf00a9106b2 828e1a71f3cb8a34a97ff9a48dbdf64c9445f9fb)
if [[ "$#" == "0" ]]; then
    FORMAT_COMMITS=(${DEFAULT_FORMAT_COMMITS[@]})
else
    FORMAT_COMMITS=("$@")
fi

# Update remote
git fetch origin &> /dev/null
# Update submodules
git submodule update --init --recursive &> /dev/null

# Process each formatting commit
for commit in "${FORMAT_COMMITS[@]}"; do
    if git rebase --show-current-patch &> /dev/null ; then
        git -c core.editor=true rebase --continue
        if git rebase --show-current-patch &> /dev/null ; then
            echo "Conflicts need resolution before continuing with formatting!"
            git diff --name-only
            exit 1
        fi
    fi

    if ! git merge-base --is-ancestor "$commit"~ HEAD ; then
        echo "Rebase up until formatting $commit..."
        # Rebase up until formatting
        if ! git rebase "$commit"~ ; then
            echo "Conflicts need resolution before continuing with formatting!"
            git diff --name-only
            exit 1
        fi
        # Update submodules
        git submodule update --init --recursive &> /dev/null
    fi

    if ! git merge-base --is-ancestor "$commit" HEAD ; then
        # Format code
        echo "Rebase formatting $commit..."
		format_args=
		if [[ -n "$CHANGED" ]]; then
			format_args="-c"
		fi
        if ! git rebase -X theirs -x "$SCRIPT_DIR/format_and_amend.sh $format_args" "$commit"; then
            echo "Error formatting, must fix manually before continuing!"
            exit 1
        fi
    fi
done

# Rebase the rest of the upstream branch
echo "Rebase $UPSTREAM_BRANCH..."
git rebase "$UPSTREAM_BRANCH"

# Update submodules
git submodule update --init --recursive &> /dev/null
