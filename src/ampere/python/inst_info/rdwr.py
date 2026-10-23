#!/usr/bin/env python3
# Copyright (c) 2023-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from __future__ import annotations

import argparse
import collections
import copy
import os
import re
import sys
from typing import List, Mapping, Set, Tuple

import openpyxl
from openpyxl.styles import Font, PatternFill
from openpyxl.styles.borders import Border, Side
from openpyxl.utils import get_column_letter


def find_root_dir(s: str) -> str:
    p = os.path.abspath(s)
    p = re.sub(r"src/ampere.*", "", p)
    return p


PAN_ROOT = find_root_dir(sys.argv[0])
ampere_dirs = ["src/ampere/python/xlat/src"]
sys.path += [os.path.join(PAN_ROOT, x) for x in ampere_dirs]
import ampere.xlat
import gen_inst_perf_info


def read_xls(input_xls: str) -> List[dict]:
    wb = openpyxl.load_workbook(input_xls)
    recs = []
    for i, row in enumerate(wb["Instruction info"].rows):
        # replace None values with empty string
        values = [x.value if x.value else "" for x in row]
        if i == 0:
            header = values
            uops_idx = header.index("Uops")
        else:
            values[uops_idx] = gen_inst_perf_info.uops_sort(values[uops_idx])
            recs.append(dict(zip(header, values)))
    return recs


def write_xls(output_xls: str, recs: List[dict]):
    red = "00FF0000"
    pink = "00FFC0CB"
    green = "0000FF00"
    spring_green = "0000FF7F"
    light_blue = "0099CCFF"
    yellow = "00FFFF00"

    thin_border = Border(
        left=Side(style="thin"), right=Side(style="thin"), top=Side(style="thin"), bottom=Side(style="thin")
    )

    wb = openpyxl.Workbook()
    wb.remove(wb["Sheet"])
    sheet = wb.create_sheet("Instruction info")
    headers = list(recs[0].keys())
    for col, fld in enumerate(headers, 1):
        sheet.cell(row=1, column=col).value = fld
        sheet.cell(row=1, column=col).fill = PatternFill("solid", fgColor=light_blue)
        sheet.cell(row=1, column=col).border = thin_border

    # combine recs and deleted_xlat into one sorted thing & emit
    tuple_headers = headers[: headers.index("Uops") + 1]  # grab the headers up to and including the Uops field

    def _mktuple(rec: dict) -> Tuple:
        return tuple([rec[v] for v in tuple_headers])

    combined_recs = sorted(recs, key=_mktuple)

    for row, rec in enumerate(combined_recs, 2):
        color = None
        if "Status" in rec:
            if rec["Status"] == "DELETED":
                color = pink
            elif rec["Status"] == "NEW":
                color = spring_green
            elif rec["Status"] == "STALE":
                color = red
        for col, fld in enumerate(headers, 1):
            sheet.cell(row=row, column=col).value = rec[fld]
            if color:
                sheet.cell(row=row, column=col).fill = PatternFill("solid", fgColor=color)
            sheet.cell(row=row, column=col).border = thin_border

    last_column_letter = get_column_letter(len(headers) + 1)
    nrows = len(combined_recs)
    sheet.auto_filter.ref = f"A1:{last_column_letter}{nrows + 1}"
    sheet.freeze_panes = "C2"
    wb.save(filename=output_xls)


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("-i", "--input-xls", required=True, help="Input xls file")
    parser.add_argument("-o", "--output-xls", required=True, help="Output xls file")
    parser.add_argument(
        "-c", "--core", choices=["furiosa", "ripley", "trinity", "connor"], help="select core decoder", required=True
    )
    args = parser.parse_args()
    return args


def update_section_group(ntuple_fields: int, recs: List[dict]):
    """Copy section and group from the older DELETED records to the
    newly created records, if they have nothing already."""
    headers = list(recs[0].keys())
    tuple_headers = headers[0:ntuple_fields]
    sec_group = collections.defaultdict(set)
    sections_count = collections.defaultdict(set)
    groups_count = collections.defaultdict(set)
    for rec in recs:
        rec_tvals = tuple([rec[v] for v in tuple_headers])
        if rec["Status"] == "DELETED":
            cls = rec["Class"]
            sec_group[cls].add((rec["Section"], rec["Group"]))
            sections_count[cls].add(rec["Section"])
            groups_count[cls].add(rec["Group"])
    for rec in recs:
        if rec["Status"] in ["SAME", "NEW", "APPROVED"]:
            cls = rec["Class"]
            if cls in sec_group:
                sec_grp_set = sec_group[cls]
                if len(sec_grp_set) == 1:
                    # grab only element via one-iteration for-loop
                    for sec, grp in sec_grp_set:
                        if rec["Section"] == "":
                            rec["Section"] = sec
                        if rec["Group"] == "":
                            rec["Group"] = grp
                elif len(sections_count[cls]) == 1:
                    # grab only element via one-iteration for-loop
                    for sec in sections_count[cls]:
                        if rec["Section"] == "":
                            rec["Section"] = sec
                elif len(groups_count[cls]) == 1:
                    # grab only element via one-iteration for-loop
                    for grp in groups_count[cls]:
                        if rec["Group"] == "":
                            rec["Group"] = grp


def merge(recs: List[dict], xlat_inst_info: Set[Tuple], inst_uops_dict: Mapping[Tuple, str]) -> (int, List[dict]):
    """xlat_inst_info has updated computed latencies & throughputs.
    This sets the 'Status' field for XLSX input records based on if
    they match or differ from the freshly computed XLAT values.
    Return the new records. XLAT entries that don't match
    are probably fresh and just corrected in this last run."""
    headers = list(recs[0].keys())
    for tup in xlat_inst_info:
        ntuple_fields = len(tup)  # number of fields in the tuple
        break  # just look at first record
    tuple_headers = headers[:ntuple_fields]

    # make a tuple from the record
    # store it in a mapping for SAME, NEW or DELETED records.
    # the mapping points to the rest of the row information.
    xlat_tuples = copy.deepcopy(xlat_inst_info)
    nrecs = []
    for i, rec in enumerate(recs):
        rec_tvals = tuple([rec[v] for v in tuple_headers])
        if not rec["Instruction"]:
            continue
        if rec["Status"] in ["STALE", "DELETED"]:
            # these didn't match before. They are just along for
            # the ride.
            nrecs.append(rec)
            continue
        elif rec["Status"] == "NEW":
            # delete the old NEW recs because we are recomputing them.
            continue
        elif rec_tvals in xlat_tuples:
            if rec["Status"] in ["APPROVED", "SAME"]:
                # we delete 'same' things from xlat_tuples so that the
                # remaining tuples can ultimately form the 'NEW'
                # set.
                xlat_tuples.remove(rec_tvals)
                nrecs.append(rec)
            else:
                # if APPROVED, we now think the value computed is different; Should revert the approval.
                # if SAME, that is some sort of other bug
                # sys.exit(f"Unexpected record {rec['Status']}")
                raise ValueError(f"Unexpected record {rec['Status']} for {rec['Instruction']}")
        else:
            # rec does not currently appear in XLAT computed values
            if rec["Status"] == "APPROVED":
                # we approved the parameters earlier of a NEW record
                # but now we recomputed something and it no longer
                # matches what the xlat fresh computation is giving
                # us. So we have a new category: STALE. We will have
                # to manually clean up stale records, and probably
                # approve the NEW record emitted for this encoding.
                rec["Status"] = "STALE"
                nrecs.append(rec)
            elif rec["Status"] == "SAME":
                # these previously matched what the xlat freshly
                # computed. But they no longer match. Should become
                # "DELETED"
                rec["Status"] = "DELETED"
                nrecs.append(rec)
            else:
                raise ValueError(f"Unexpected record {rec['Status']} for {rec['Instruction']}")

    recs = nrecs

    # if the computed tuples changed with this run of the script. So they COULD match 'DELETED' records
    # in which case they should become Status = "SAME".
    for i, rec in enumerate(recs):
        rec_tvals = tuple([rec[v] for v in tuple_headers])
        if rec["Status"] == "DELETED":
            if rec_tvals in xlat_tuples:
                # print(f"CONVERTING {rec['Class']:20} {rec['Group']}")
                rec["Status"] = "SAME"
                xlat_tuples.remove(rec_tvals)

    # Add the remaining unmatched XLAT tuples to the recs. These
    # "FRESH" records will show up when we change formulas for
    # computing latency/tpt in the XLAT processing.
    for tup in xlat_tuples:
        rec = {fld: val for fld, val in zip(tuple_headers, tup)}
        # rec['Uops'] = inst_uops_dict[tup]
        for fld in ["Notes", "Section", "Group", "Latency override", "Throughput override"]:
            rec[fld] = ""
        # set Status to NEW so that we can re-read the XLS file
        rec["Status"] = "NEW"
        recs.append(rec)

    return ntuple_fields, recs


def work():
    args = parse_args()
    recs = read_xls(args.input_xls)

    # read xlat and make data structures based on that; writes ampere.xlat.translations global used by gen_inst_info()
    gen_inst_perf_info.read_xlat(args.core)
    partial_inst_info, inst_uops_dict = gen_inst_perf_info.gen_inst_info(args.core)
    xlat_inst_info = set()
    # add the uops to the tuple
    for inst in partial_inst_info:
        xlat_inst_info.add(inst + (inst_uops_dict[inst],))
    ntuple_fields, recs = merge(recs, xlat_inst_info, inst_uops_dict)
    update_section_group(ntuple_fields, recs)
    write_xls(args.output_xls, recs)


if __name__ == "__main__":
    work()
