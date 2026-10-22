#!/usr/bin/env python3
# Copyright (c) 2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Summarize study failures locally."""

import logging
import re
from multiprocessing import cpu_count

from git import Repo
from panthera import ArgumentParser, setup_logging
from panthera.cheetah import RollupStudy
from panthera.cheetah.bucketing import Bucketer
from panthera.cheetah.logfile import LogParsing
from panthera.report.notify import PrintNotifier
from panthera.rollup import CONFIGURATION


def parse_args():
    parser = ArgumentParser(description=__doc__, tool="report.failures")

    parser.add_argument("study", help="Study (JSON) path")
    parser.add_argument("--title", default="Panthera failure summary", help="Report title")
    parser.add_argument("--template", default="failure_buckets.html.j2", help="Report template")
    parser.add_argument("--per", default=CONFIGURATION, help="Split jobs by")
    parser.add_argument("--git-repo", default=".", help="Git repo path")
    parser.add_argument("--from-git-tag", help="Optional from git tag")
    parser.add_argument("--skip-incomplete", action="store_true", help="Skip if all failures are incomplete")
    parser.add_argument(
        "-j",
        "--parallel",
        metavar="N",
        type=int,
        default=max(1, min(8, int(cpu_count() / 4))),
        help="Amount of threads used for logfile parsing",
    )

    return parser.parse_args()


def load_study(args):
    log_parsing = LogParsing(parallel=args.parallel)
    study = RollupStudy.from_json(args.study, parsed_logs=log_parsing)

    remove_most_paths = re.compile(r".*/([\w_]+\.(cc|h):)")
    all_failed_jobs = list(study.failed_jobs())
    all_jobs = list(study.all_jobs())
    summary = {}
    buckets = {}

    for name in sorted(study.unique_meta_values(args.per)):
        failed_jobs = [j for j in all_failed_jobs if j.meta.get(args.per) == name]
        jobs = [j for j in all_jobs if j.meta.get(args.per) == name]

        summary[name] = {
            "total": len(jobs),
            "passed": sum(j.successful for j in jobs),
            "failed": len(failed_jobs),
            "incomplete": sum(j.incomplete for j in failed_jobs),
        }

        if failed_jobs:
            bucketer = Bucketer()
            bucketer.bucket_jobs(failed_jobs)
            buckets[name] = bucketer.buckets
            for bucket in buckets[name]:
                bucket.key = remove_most_paths.sub(r"\1", bucket.key)

    return summary, buckets


def gather_commits(args):
    repo = Repo(args.git_repo)
    return [
        {
            "sha": commit.hexsha[:8],
            "author": commit.author.name,
            "description": commit.summary,
        }
        for commit in repo.iter_commits(f"{args.from_git_tag}..HEAD")
    ]


def main(args):
    setup_logging()
    summary, buckets = load_study(args)

    if args.skip_incomplete and all(s["failed"] == s["incomplete"] for s in summary.values()):
        logging.info("All failed jobs incomplete, skipping...")
        return

    commits = []
    if args.from_git_tag:
        commits = gather_commits(args)

    renderer = PrintNotifier(args.template)
    content = renderer.render(
        title=args.title,
        summary=summary,
        buckets=buckets,
        commits=commits,
    )
    renderer.notify(content)


if __name__ == "__main__":
    main(parse_args())
