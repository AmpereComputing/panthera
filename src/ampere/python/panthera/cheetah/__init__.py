# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Run many simulations quickly"""

__tools__ = [
    "errors",
    "run",
    "xml_report",
]

import argparse
import logging
import os
import re
from copy import deepcopy
from multiprocessing import cpu_count

from panthera import setup_logging

from .batch import Batch
from .batchers import mapping as batchers
from .cmd import Cmd, CmdPool, CmdSeq
from .job import Job
from .study import RollupStudy
from .study import StudyBuilder as Study

log = logging.getLogger(__name__)


class Args:
    def __init__(self, **kwargs):
        self.__dict__.update(kwargs)

    def merge(self, **kwargs):
        self.__dict__.update(kwargs)
        return self


def add_cheetah_args(parser):
    parser.add_argument("-s", "--study", "--name", dest="study", help="Study name")
    parser.add_argument(
        "--run",
        default="slurm",
        choices=list(batchers.keys()),
        help="run the jobs with batch system",
    )
    parser.add_argument("-o", "--output", help="Top level output directory")
    parser.add_argument(
        "--remove",
        action="store_true",
        help="remove any existing study directory before running",
    )
    parser.add_argument(
        "--rerun",
        action="store_true",
        help="relaunch unfinished or failed jobs",
    )
    parser.add_argument(
        "--extend",
        action="store_true",
        help="add new jobs to study and only run those",
    )
    parser.add_argument(
        "--filter",
        help="filter job (names) based on a regular expression",
    )

    parser.add_argument(
        "--validate",
        metavar="N",
        nargs="?",
        const=100,
        help="run one command set for each configuration",
    )
    parser.add_argument(
        "--run-validate",
        default="parallel",
        choices=list(batchers.keys()),
        help="validate jobs with batch system",
    )
    parser.add_argument(
        "--dryrun",
        "--dry-run",
        dest="dry_run",
        action="store_true",
        help="Do not launch study, only generate collateral",
    )

    parser.add_argument("--xml-report", help="XML report path")

    Batch.parser_add_args(parser)
    for batcher in batchers.values():
        batcher.parser_add_args(parser)

    advanced = parser.add_argument_group("Advanced options", "Options which are usually unneeded")
    advanced.add_argument(
        "--study-creation-threads",
        metavar="N",
        default=max(1, min(8, int(cpu_count() / 4))),
        help="Amount of threads used for study (i.e. JSON) creation",
    )
    advanced.add_argument(
        "--allow-debug",
        action="store_true",
        help="allow debug binaries (for fast_regress, mainly)",
    )
    advanced.add_argument("--debug", action="store_true", help="Enable debug logging")
    advanced.add_argument(
        "--no-jobfile-output",
        action="store_true",
        help="No study/job file output, for processing study definition and jobs only (i.e. testing)",
    )


def argument_parser(cheetah_args=True, **kwargs):
    """Setup basic argument parser."""
    parser = argparse.ArgumentParser(conflict_handler="resolve", **kwargs)
    if cheetah_args:
        add_cheetah_args(parser)

    return parser


def _run_study(
    study,
    batcher_name=None,
    validate=None,
    threads=1,
    dry_run=False,
    allow_debug=True,
    no_jobfile_output=False,
    args=None,
):
    if validate and no_jobfile_output:
        raise ValueError("--no-jobfile-output cannot be used with --validate")

    study.build(
        validate=validate,
        allow_debug=allow_debug,
        threads=threads,
        process_only=no_jobfile_output,
    )

    batcher_name = batcher_name or "slurm"
    workdir = getattr(args, "workdir", None)
    batcher = batchers[batcher_name].from_study(
        study,
        workdir=workdir,
        args=args,
    )
    if not no_jobfile_output:
        batcher.setup()
    if not dry_run and not no_jobfile_output:
        batcher.run()


def _filter_cmds(cmds, job_filter):
    job_filter_regex = re.compile(job_filter)

    def filter_cmd(cmd):
        return bool(job_filter_regex.search(Job.name_from_cmds(cmd)))

    return cmds.select(filter_cmd)


def validate_study(study, cmds, validate, check=True, dry_run=False, args=None) -> RollupStudy:
    """Validate study using cheetah."""
    cmds_per_key = {}
    for cmd in reversed(cmds):
        cmd_validate_key = Job.validation_key_from_cmds(cmd)
        if cmd_validate_key not in cmds_per_key:
            cmds_per_key[cmd_validate_key] = cmd.copy()

    validation_cmds = CmdPool(list(cmds_per_key.values()))
    validation_path = os.path.join(study.directory, "_validate")
    validation_study = Study(
        "validate",
        validation_cmds,
        directory=validation_path,
    )
    validation_study.setup(remove=True)

    log.info(f"validating {len(validation_cmds.cmds)} commands in study")

    batcher_name = getattr(args, "run_validate", "parallel")
    _run_study(
        validation_study,
        batcher_name=batcher_name,
        validate=validate,
        dry_run=dry_run,
        args=args,
    )

    rollup = validation_study.as_rollup()
    if not check:
        return rollup

    failed = list(job.log_abspath for job in rollup.failed_jobs())
    if failed:
        jobs = "\n".join(failed)
        raise RuntimeError(f"validation failed, {len(failed)} configurations are invalid:\n\n{jobs}")

    log.info(f"validation successful, all configurations valid")
    return rollup


def create_xml_report(study, rollup=None, path=None):
    """Create XML report for study."""
    if rollup is None:
        rollup = study.as_rollup()
        rollup.parse_logs()

    path = path or os.path.join(rollup.name, "report.xml")
    report = rollup.create_xml_report()
    report.write(path)


def run_study(study_name, cmds, args=None):
    """Run study using cheetah."""
    setup_logging(debug=args.debug)

    if args.no_jobfile_output and not args.dry_run:
        raise ValueError("--no-jobfile-output requires --dryrun")

    if args.filter:
        cmds = _filter_cmds(cmds, args.filter)

    study = Study(
        study_name,
        cmds,
        directory=args.output,
        rerun=args.rerun,
        extend=args.extend,
    )

    study.setup(remove=args.remove)
    if args.validate:
        log.debug("Validating study")
        validate_args = deepcopy(args)
        setattr(validate_args, "rerun", None)
        setattr(validate_args, "extend", None)
        validate_study(study, cmds, args.validate, args=args)

    if args.no_jobfile_output:
        log.debug(f"Processing study only")
    elif args.dry_run:
        log.debug(f"Creating study for {args.run}")
    elif args.rerun:
        log.debug(f"Rerunning study with {args.run}")
    elif args.extend:
        log.debug(f"Extending study with {args.run}")
    else:
        log.debug(f"Running study with {args.run}")

    _run_study(
        study,
        batcher_name=args.run,
        threads=args.study_creation_threads,
        dry_run=args.dry_run,
        allow_debug=args.allow_debug,
        no_jobfile_output=args.no_jobfile_output,
        args=args,
    )

    if args.xml_report:
        create_xml_report(study, path=args.xml_report)

    return study
