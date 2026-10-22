#!/usr/bin/env python3
# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


import argparse
import logging
import sys

import pandas as pd

import panthera.enums.chi_opcodes

logger = logging.getLogger(__name__)


def verify_mem_stream(args, summary_df: pd.DataFrame) -> bool:
    passed: bool = True
    tolerance: float = args.tolerance

    for core in range(0, args.core_count):
        read: int = get_event_count(summary_df, f".*core_{core}.send_req/ReadShared/allow_retry:1")
        read += get_event_count(summary_df, f".*core_{core}.send_req/ReadNotSharedDirty/allow_retry:1")
        evict: int = get_event_count(summary_df, f".*core_{core}.send_req/Evict/allow_retry:1")
        writeevict_or_evict: int = get_event_count(
            summary_df, f".*core_{core}.send_req/WriteEvictOrEvict/allow_retry:1"
        )
        stash_reject: int = get_event_count(summary_df, f".*core_{core}.send_rsp/SnpResp/data_pull:0")

        evict += stash_reject  # If we reject a stash we don't send an evict out, we shouldn't fail the test for this

        read_evict_diff = round(abs(read - evict) / evict * 100, 2) if evict != 0 else 100
        read_writeevict_or_evict_diff = (
            round(abs(read - writeevict_or_evict) / writeevict_or_evict * 100, 2) if writeevict_or_evict != 0 else 100
        )
        evict_writeevict_or_evict_diff = (
            round(abs(evict - writeevict_or_evict) / writeevict_or_evict * 100, 2) if writeevict_or_evict != 0 else 100
        )

        logger.info(f"Core {core}: ReadShared({read}), WriteEvictOrEvict({writeevict_or_evict}), Evict({evict})")

        if read_evict_diff > tolerance:
            logger.error(
                f"Core {core}: ReadShared({read}) count out of range with Evict/SnpRsp_DatPull:0({evict}), diff = {read_evict_diff}%."
            )
            passed = False
        if read_writeevict_or_evict_diff > tolerance:
            logger.error(
                f"Core {core}: ReadShared({read}) count out of range with WriteEvictOrEvict({writeevict_or_evict}) count, diff = {read_writeevict_or_evict_diff}%."
            )
            passed = False
        if evict_writeevict_or_evict_diff > tolerance:
            logger.error(
                f"Core {core}: Evict/SnpRspDatPull:0({evict}) count out of range with WriteEvictOrEvict({writeevict_or_evict}) count, diff = {evict_writeevict_or_evict_diff}%."
            )
            passed = False

    if not args.dry_run:
        summary_df["verify_mem_streaming"] = passed
    return passed


def verify_stash_reject_rate(args, summary_df: pd.DataFrame) -> bool:
    passed: bool = True

    for core in range(0, args.core_count):
        stash_accept: int = get_event_count(summary_df, f".*core_{core}.send_rsp/SnpResp/data_pull:1")
        stash_reject: int = get_event_count(summary_df, f".*core_{core}.send_rsp/SnpResp/data_pull:0")
        stash_total: int = stash_accept + stash_reject
        logger.info(f"Core:{core} Stashes={stash_accept}, Stash Rejects={stash_reject}")
        stash_reject_rate = round(stash_reject / stash_total * 100, 1) if stash_total != 0 else 0
        if stash_reject_rate > args.tolerance:
            passed = False
            logger.error(f"Core:{core} Stash reject rate high at {stash_reject_rate}%")

    if not args.dry_run:
        summary_df["verify_stash_reject_rate"] = passed
    return passed


def verify_retry_rate(args, summary_df: pd.DataFrame) -> bool:
    passed: bool = True

    comp = args.component
    for idx in range(0, args.count):
        reqs: int = get_event_count(summary_df, f".*{comp}_{idx}.recv_req/.*/allow_retry:1")
        retry_reqs: int = get_event_count(summary_df, f".*{comp}_{idx}.recv_req/.*/allow_retry:0")
        total_reqs: int = reqs + retry_reqs
        retry_pct = round(retry_reqs / total_reqs * 100, 1) if total_reqs != 0 else 0
        logger.info(f"Component:{comp}_{idx} Total Requests={total_reqs}, Retry Requests={retry_reqs}")

        if retry_pct > args.tolerance:
            passed = False
            logger.error(f"Component:{comp}_{idx} Retry rate high at {retry_pct}%")

    if not args.dry_run:
        summary_df[f"verify_retry_{comp}_retry_rate"] = passed
    return passed


def verify_pxha_eq_distributed(args, summary_df: pd.DataFrame) -> bool:
    passed: bool = True
    pxha_min: int = sys.maxsize
    pxha_max: int = 0
    for pxha_num in range(0, args.pha_count):
        reqs: int = get_event_count(summary_df, f".*pxha_{pxha_num}.recv_req/ReadShared/allow_retry:1")
        reqs += get_event_count(summary_df, f".*pxha_{pxha_num}.recv_req/ReadNotSharedDirty/allow_retry:1")
        reqs += get_event_count(summary_df, f".*pxha_{pxha_num}.recv_req/MakeUnique/allow_retry:1")
        if reqs < pxha_min:
            pxha_min = reqs
        elif reqs > pxha_max:
            pxha_max = reqs
        logger.info(f"PxHA {pxha_num}: reqs = {reqs}")

    pct_diff: float = round(((pxha_max - pxha_min) / pxha_min * 100), 2) if pxha_min != 0 else 100

    logger.info(f"PxHA Max percent difference {pct_diff}%")
    logger.info(f"Max:{pxha_max}, Min:{pxha_min}")
    if pct_diff > args.tolerance:
        passed = False
        logger.error(
            f"PxHAs are not equally targeted/hashed for this workload with a max difference of {pct_diff}% between PHAs"
        )

    if not args.dry_run:
        summary_df["verify_pxha_eq_distributed"] = passed
    return passed


def verify_pmha_eq_distributed(args, summary_df: pd.DataFrame) -> bool:
    passed: bool = True
    pmha_min: int = sys.maxsize
    pmha_max: int = 0
    for pmha_num in range(0, args.pha_count):
        reqs: int = get_event_count(summary_df, f".*pmha_{pmha_num}.recv_req/ReadShared/allow_retry:1")
        reqs += get_event_count(summary_df, f".*pmha_{pmha_num}.recv_req/ReadClean/allow_retry:1")
        reqs += get_event_count(summary_df, f".*pmha_{pmha_num}.recv_req/MakeUnique/allow_retry:1")
        if reqs < pmha_min:
            pmha_min = reqs
        elif reqs > pmha_max:
            pmha_max = reqs
        logger.info(f"pmha {pmha_num}: reqs = {reqs}")

    pct_diff: float = round(((pmha_max - pmha_min) / pmha_min * 100), 2) if pmha_min != 0 else 100

    logger.info(f"pmha Max percent difference {pct_diff}%")
    logger.info(f"Max:{pmha_max}, Min:{pmha_min}")
    if pct_diff > args.tolerance:
        passed = False
        logger.error(
            f"pmhas are not equally targeted/hashed for this workload with a max difference of {pct_diff}% between PHAs"
        )

    if not args.dry_run:
        summary_df["verify_pmha_eq_distributed"] = passed
    return passed


def verify_cleaninvalid_rate(args, summary_df: pd.DataFrame) -> bool:
    passed: bool = True

    for pha in range(0, args.pha_count):
        reqs: int = get_event_count(summary_df, f".*pxha_{pha}.recv_req/.*/allow_retry:1")
        cleaninvalids: int = get_event_count(summary_df, f".*pxha_{pha}.send_snp/SnpCleanInvalid")

        pct = round((cleaninvalids / reqs * 100), 1) if reqs != 0 else 0

        logger.info(f"Pxha {pha}: Reqs({reqs}), SnpCleanInvalids({cleaninvalids}) = {pct}%")

        if pct > args.tolerance:
            passed = False
            logger.error(f"PxHA {pha}: Has high rate of SnpCleanInvalids at {pct}%")

    if not args.dry_run:
        summary_df["verify_cleaninvalid_rate"] = passed
    return passed


def get_event_count(df: pd.DataFrame, event_regex: str) -> int:
    return df.filter(regex=event_regex).sum(axis=1).item()


def parse_subcmd_args(command_args, cmds) -> list[list[str]]:
    global_args: list[str] = []
    subcmd: list[list[str]] = []
    build_args: list[str] = []
    global_cmd: bool = True

    for arg in command_args:
        if arg in cmds:
            # argument is not an argument but a subcommand.
            if global_cmd:
                global_args = build_args
            else:
                subcmd.append(global_args + build_args)
            build_args = [arg]
            global_cmd = False
        else:
            build_args.append(arg)

    subcmd.append(global_args + build_args)
    return subcmd


def main():
    parser = argparse.ArgumentParser(description="Verify matrix study completed as expected")
    parser.add_argument("--csv", required=True, type=str, help="The summary.csv file")
    parser.add_argument("--dry-run", action="store_true", help="Don't update the summary verify columns with results")
    parser.add_argument(
        "--verbose", action="store_true", help="Print human readable information about verification results"
    )
    subparsers = parser.add_subparsers(title="verify command", description="valid subcommands")

    parser_stash_reject_rate = subparsers.add_parser(
        "stash_reject_rate", help="Check that stash reject rate is under a threshold"
    )
    parser_stash_reject_rate.add_argument(
        "--tolerance",
        type=int,
        help="Amount of unequal distribution allowed between PHAs in terms of percent",
        default=4,
    )
    parser_stash_reject_rate.add_argument("--core_count", type=int, help="Number of cores", default=64)
    parser_stash_reject_rate.set_defaults(func=verify_stash_reject_rate)

    parser_retry_rate = subparsers.add_parser("retry_rate", help="Check that retries our under a threshold")
    parser_retry_rate.add_argument(
        "--tolerance",
        type=int,
        help="Amount of unequal distribution allowed between PHAs in terms of percent",
        default=4,
    )
    parser_retry_rate.add_argument(
        "--component", choices=["pxha", "pmha", "cmc", "memory"], help="Component to verify retires on", default="core"
    )
    parser_retry_rate.add_argument("--count", type=int, help="Number of cores", default=1)
    parser_retry_rate.set_defaults(func=verify_retry_rate)

    parser_pxha_eq_distributed = subparsers.add_parser(
        "pxha_eq_distributed", help="Check to see that PxHAs were exercised equally"
    )
    parser_pxha_eq_distributed.add_argument(
        "--tolerance",
        type=int,
        help="Amount of unequal distribution allowed between PHAs in terms of percent",
        default=4,
    )
    parser_pxha_eq_distributed.add_argument("--pha_count", type=int, help="Number of PHAs", default=24)
    parser_pxha_eq_distributed.set_defaults(func=verify_pxha_eq_distributed)

    parser_pmha_eq_distributed = subparsers.add_parser(
        "pmha_eq_distributed", help="Check to see that PmHAs were exercised equally"
    )
    parser_pmha_eq_distributed.add_argument(
        "--tolerance",
        type=int,
        help="Amount of unequal distribution allowed between PHAs in terms of percent",
        default=4,
    )
    parser_pmha_eq_distributed.add_argument("--pha_count", type=int, help="Number of PHAs", default=12)
    parser_pmha_eq_distributed.set_defaults(func=verify_pmha_eq_distributed)

    parser_cleaninvalid_rate = subparsers.add_parser(
        "cleaninvalid_rate", help="Check to make sure we are getting few SnpCleanInvalids received by the cores"
    )
    parser_cleaninvalid_rate.add_argument(
        "--tolerance", type=int, help="Percentage of (CleanInvalids / cores*.send_req) allowed", default=1
    )
    parser_cleaninvalid_rate.add_argument("--pha_count", type=int, help="Number of cores", default=24)
    parser_cleaninvalid_rate.set_defaults(func=verify_cleaninvalid_rate)

    parser_mem_stream = subparsers.add_parser(
        "mem_stream",
        help="Check to see that we are streaming from memory. (i.e. Equal number of Read/WriteEvictOrEvict(l2)/Evict(l3) from injectors)",
    )
    parser_mem_stream.add_argument(
        "--tolerance",
        type=int,
        help="Amount of unequal distribution allowed between PHAs in terms of percent",
        default=2,
    )
    parser_mem_stream.add_argument("--core_count", type=int, help="Number of cores", default=64)
    parser_mem_stream.set_defaults(func=verify_mem_stream)

    # Parse commandline arguments
    subcmds = [c for c in subparsers.choices]
    subcmd_args = parse_subcmd_args(sys.argv[1:], subcmds)
    cmds_args = [parser.parse_args(args) for args in subcmd_args]
    global_args = cmds_args[0]  # Provide a way to get to global args

    logging.basicConfig(format="[%(funcName)s:%(levelname)s] - %(message)s")
    logger.setLevel(logging.INFO if global_args.verbose else logging.WARNING)

    # Open summary dataframe
    with open(global_args.csv) as csv_file:
        summary_df: pd.DataFrame = pd.read_csv(csv_file)

    # Execute the verification functions
    rc: bool = True
    for cmd_args in cmds_args:
        failed = cmd_args.func(cmd_args, summary_df)
        if not failed:
            rc = False

    # Write summary file back out.  Create/Update a column called verify_matrix
    # this column should always exists and contain the result of all tests.
    summary_df["verify_matrix"] = rc
    if not global_args.dry_run:
        with open(global_args.csv, "w") as csv_file:
            summary_df.to_csv(csv_file)

    return rc


if __name__ == "__main__":
    rc: int = main()
    sys.exit(rc)
