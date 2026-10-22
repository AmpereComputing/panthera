#!/usr/bin/env python3
# Copyright (c) 2022-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Generate a table of performance information for instructions, or a
more nicely formatted document for human consumption.

This script can generate a table with information about all
instructions, and optionally merge it with a previously generated
version of the same table. Then, after a human has added
categorization information, it can take the updated table and output a
more nicely formatted table of instruction groups.

Run this script from the top of the panthera git repo so it can find
the python imports.

Example usage, 3 steps:

(1) Generate a fresh table based on the XLAT in the current model
% ./gen_inst_perf_info.py -o perfout.xlsx -f table -c connor

(2) Merge info from current XLAT with a previously generated and
annotated table. All lines will be tagged with a status:
   SAME => this line had all the same info the input and the current XLAT; section/group are copied over
   NEW => this line is in the current XLAT but not the input doc
   DELETED => this line was in the input doc but is no longer in the XLAT
% ./gen_inst_perf_info.py --remove-deleted -i perf-info-trinity.xlsx -o perfout.xlsx -f table -c connor

(3) Iterate using rdwr.py to update the NEW lines if necessary and
setting the Status filed to APPROVED when they make sense. rdwr.py
calls into this script to re-compute latencies for the new
instructions when it reads in the xlsx file. STALE lines can arise
from a previously APPROVED line that gets recomputed. Those can be
manually removed after studying them.

% ./rdwr.py -i perfout.xlsx -o perfout1.xlsx -c connor
% ./rdwr.py -i perfout1.xlsx -o perfout2.xlsx -c connor
% ./rdwr.py -i perfout2.xlsx -o perfout3.xlsx -c connor
...
% ./rdwr.py -i perfout11.xlsx -o perfout12.xlsx -c connor

(4) Generate the nicely formatted summary in xlxx or latex format
% ./gen_inst_perf_info.py -i perfout12.xlsx -o perfdoc.xlsx --remove-deleted -f doc   -c connor
% ./gen_inst_perf_info.py -i perfout12.xlsx -o perfdoc.tex  --remove-deleted -f latex -c connor

"""

from __future__ import annotations

import argparse
import collections
import os
import re
import sys

import latex_table
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

# Columns of the instruction info table
table_fields = [
    "Instruction",
    "Class",
    "Description",
    "Latency",
    "Throughput",
    "Uop count",
    "Pipelines",
    "FP unit",
    "Uops",
    "Notes",
    "Section",
    "Group",
    "Latency override",
    "Throughput override",
    "Status",
]

# Columns of the summary document
doc_fields = [
    "Group",
    "Instructions",
    "Latency",
    "Throughput",
    "Uop count",
    "Pipelines",
    "FP unit",
    "Notes",
]

# Translation of FSU unit names in yaml to names used in these tables
# Ma and Mas are the same
# Hp and Hps are the same
# Au, Su, and Xu all have the same perf characteristics so combine them into SI
fsu_unit_map = {
    "Au": "SI",
    "Bu": "BU",
    "By": "BY",
    "Cu": "CU",
    "Dv": "DV",
    "Hp": "HP",
    "Hps": "HP",
    "Ma": "MA",
    "Mas": "MA",
    "Mu": "MU",
    "Su": "SI",
    "Xu": "SI",
}

# The execution width of each pipe or group of pipes that a uop can execute on
pipe_tpt_map = {
    "AB": 4,
    "A": 2,
    "B": 2,
    "BS": 1,
    "S": 2,
    "L": 2,
    "XY": 2,
    "X": 1,
    "Y": 1,
    "Z": 1,
}

# List of instructions to leave out (barriers, exclusives, EL1 only, etc.)
excluded_mnemonics = [
    "CLREX",
    "DMB",
    "DRPS",
    "DSB",
    "HINT",
    "LDGM",
    "MRS",
    "MSR",
    "STGM",
    "STZGM",
    "SYS",
    "WFET",
    "WFIT",
    "NOP",
    "ESB",
]
excluded_starts = ["ERET", "LDAP", "STLUR"]

# Number of columns in the summary document which come from the input table
DOC_COLUMNS = 5


def uops_sort(str_of_uops: str) -> str:
    if "|" in str_of_uops:
        sorted_sequences = sorted(str_of_uops.split("|"))
        return "|".join(sorted_sequences)
    else:
        return str_of_uops


def read_xlat(core: str):
    """read xlat/yaml and make data structures based on that.  This
    IMPLICITLY writes ampere.xlat.translations global used by
    gen_inst_info()."""
    if core == "furiosa":
        xlat_files = [
            "branch.xlat",
            "float.xlat",
            "integer.xlat",
            "memory.xlat",
            "system.xlat",
            "vector.xlat",
            "fusion.xlat",
        ]
    elif core == "ripley":
        xlat_files = [
            "branch.xlat",
            "float.xlat",
            "integer.xlat",
            "memory.xlat",
            "system.xlat",
            "vector.xlat",
            "fusion.xlat",
        ]
    elif core == "trinity":
        xlat_files = [
            "branch.xlat",
            "float.xlat",
            "integer.xlat",
            "memory.xlat",
            "system.xlat",
            "vector.xlat",
            "fusion.xlat",
        ]
    elif core == "connor":
        xlat_files = [
            "branch.xlat",
            "float.xlat",
            "integer.xlat",
            "memory.xlat",
            "system.xlat",
            "vector.xlat",
            "sve.xlat",
            "sve2.xlat",
            "memory_sve.xlat",
            "memory_sve2.xlat",
            "fusion.xlat",
        ]
    else:
        sys.exit(f"Error: unknown core {core}")

    sys.path += [os.path.join(PAN_ROOT, "src/ampere/decoders", core, "lib/python3")]
    xlat_paths = [os.path.join(PAN_ROOT, "src/ampere/decoders/json/a64.xlat")]
    xlat_paths.extend(
        os.path.join(PAN_ROOT, "src/ampere/decoders", core, "xlat", input_file) for input_file in xlat_files
    )
    xlat_globals = {}
    for input_file in xlat_paths:
        exec(
            compile(
                open(input_file, encoding="utf-8").read(),
                input_file,
                mode="exec",
            ),
            xlat_globals,
        )


def main():
    args = parse_args()
    # implicitly writes ampere.xlat.translations global used by gen_inst_info()
    read_xlat(args.core)
    input_inst_map, section_info = read_xls(args.infile, args.remove_deleted)
    partial_inst_info, inst_uops, movprfx_fusion_table = gen_inst_info(args.core)

    if args.output_format == "table":
        inst_info = set()
        # add the uops to the tuple
        for inst in partial_inst_info:
            inst_info.add(inst + (inst_uops[inst],))

        write_table_xls(args.outfile, table_fields, input_inst_map, inst_info)
    elif args.output_format == "doc":
        write_doc_xls(args.outfile, doc_fields, section_info)
    elif args.output_format == "latex":
        write_doc_latex(args.outfile, doc_fields, section_info)
        movprfx_outfile = re.sub(r"[.]tex$", "-movprfx.tex", args.outfile)
        write_doc_latex_movprfx(movprfx_outfile, movprfx_fusion_table)


def parse_args():
    parser = argparse.ArgumentParser(
        description="Read XLAT and an existing instruction perf info sheet and output an updated version"
    )
    parser.add_argument("-i", "--infile", action="store", help="input sheet")
    parser.add_argument("-o", "--outfile", action="store", help="output sheet", required=True)
    parser.add_argument(
        "-f",
        "--output-format",
        choices=["table", "doc", "latex"],
        help="table: merge existing table and new XLAT into a new table;" + "doc: produce final xlsx document",
    )
    parser.add_argument(
        "-c", "--core", choices=["furiosa", "ripley", "trinity", "connor"], help="select core decoder", required=True
    )
    parser.add_argument(
        "--remove-deleted", default=False, action="store_true", help="Remove deleted and Note=OMIT lines"
    )

    opts = parser.parse_args()
    return opts


def gen_inst_info(core):
    """Read the xlat/yaml translations. Returns (1) inst_info, a set
    of tuples of instruction properties.  (2) inst_uops, a dict,
    indexed by the inst_info tuples, providing a string representing
    the uops sequences. (3) a table of movprfx fusionable mnemonics"""

    movprfx_fusion_table = collections.defaultdict(set)
    inst_info = set()
    inst_uops = {}
    for trans, _ in ampere.xlat.translations:
        if skip_instruction(trans):
            continue

        mnemonic = trans.asmtemplate.split()[0]
        num_uops = len(trans.uops)
        uops = ",".join([u.name for u in trans.uops])
        print(f"Processing {mnemonic=} {uops=}")
        if "not_implemented" in uops:
            continue
        pipes = " ".join([get_pipes(u) for u in trans.uops])
        throughput = get_instr_tpt(trans.uops)
        fsu_unit = get_instr_fsu_unit(trans.uops)
        latency = get_instr_latency(mnemonic, trans.uops, fsu_unit, core)

        movprfx_fusion = trans.movprfx_fusion if hasattr(trans, "movprfx_fusion") else None
        if movprfx_fusion:
            # print(f"movprfx: {mnemonic=} {movprfx_fusion=}")
            movprfx_fusion_table[movprfx_fusion].add(mnemonic)

        this_info = (
            mnemonic,
            trans.__class__.__name__,
            trans.__class__.__doc__,
            latency,
            throughput,
            num_uops,
            pipes,
            fsu_unit,
        )
        inst_info.add(this_info)
        if this_info not in inst_uops:
            inst_uops[this_info] = uops
        elif uops not in inst_uops[this_info]:
            inst_uops[this_info] += "|" + uops

    new_inst_uops = {k: uops_sort(v) for k, v in inst_uops.items()}
    return (inst_info, new_inst_uops, movprfx_fusion_table)


def read_xls(infile: str, remove_deleted: bool) -> (dict, dict):
    if not infile or not os.path.isfile(infile):
        print(f"WARNING: input file does not exist: {infile}")
        return None, None
    mismatches = 0
    input_inst_map = {}
    section_info = {}

    wb = openpyxl.load_workbook(infile)
    for row in wb["Instruction info"].rows:
        if row[0].value == table_fields[0]:
            pass
        else:
            mnemonic = row[0].value
            classname = row[1].value
            description = row[2].value
            latency = row[3].value
            throughput = str(
                row[4].value
            )  # force this to str since some look like ints and some are strings like '1/2'
            num_uops = row[5].value
            pipes = row[6].value
            fsu_unit = row[7].value
            uops = row[8].value
            notes = row[9].value
            section = row[10].value
            group = row[11].value
            latency_ovrd = row[12].value
            throughput_ovrd = row[13].value
            try:
                status = row[14].value
            except:
                status = ""

            if fsu_unit is None:
                fsu_unit = ""

            if remove_deleted:
                if status in ["STALE", "DELETED"]:
                    continue
                if notes == "OMIT":
                    continue

            input_inst_map[(mnemonic, classname, description, latency, throughput, num_uops, pipes, fsu_unit, uops)] = (
                notes,
                section,
                group,
                latency_ovrd,
                throughput_ovrd,
            )

            doc_latency = latency
            doc_throughput = throughput
            if latency_ovrd is not None:
                doc_latency = latency_ovrd
            if throughput_ovrd is not None:
                doc_throughput = throughput_ovrd

            this_info = (doc_latency, doc_throughput, num_uops, pipes, fsu_unit, notes)

            if section is None or group is None:
                if notes != "OMIT":
                    print(f"missing section or group: {classname} {description} {this_info}")
                continue

            if section not in section_info:
                section_info[section] = {}

            if group not in section_info[section]:
                section_info[section][group] = collections.defaultdict(set)
            section_info[section][group][this_info].add(mnemonic)

    return (input_inst_map, section_info)


def write_table_xls(outfile, fields=None, input_inst_map=None, inst_info=None):
    """Write all the data to an xls file"""
    status_histogram = collections.defaultdict(int)
    wb = openpyxl.Workbook()
    wb.remove(wb["Sheet"])
    sheet = wb.create_sheet("Instruction info")
    for column_index, field in enumerate(fields, 1):
        sheet.cell(row=1, column=column_index).value = field
    mcol = len(fields) + 1
    full_instr_list = inst_info.copy()
    if input_inst_map is not None:
        for inst in input_inst_map:
            full_instr_list.add(inst)

    red = "00FF0000"
    green = "0000FF00"
    classname_idx = table_fields.index("Class")
    # section_idx = table_fields.index('Section')
    # group_idx = table_fields.index('Group')
    for row_index, inst in enumerate(sorted(full_instr_list), 2):
        # Write all the basic information about this instruction into the sheet
        classname = inst[classname_idx]
        # print(f'{classname=}')
        for column_index, field in enumerate(inst, 1):
            sheet.cell(row=row_index, column=column_index).value = inst[column_index - 1]

        # Now figure out where the section, description, and notes come from
        if input_inst_map and input_inst_map.get(inst):
            # This line was in the input, so copy over the extra fields
            for column_index, field in enumerate(input_inst_map[inst], len(inst) + 1):
                sheet.cell(row=row_index, column=column_index).value = input_inst_map[inst][
                    column_index - len(inst) - 1
                ]
            if inst in inst_info:
                status = "SAME"
                color = None
            else:
                status = "DELETED"
                color = red
        else:
            # This line is new -- it was not in the input
            status = "NEW"
            color = green

        ncol = len(inst) + DOC_COLUMNS + 1
        sheet.cell(row=row_index, column=ncol).value = status
        status_histogram[status] += 1
        if color:
            for col in range(ncol):
                sheet.cell(row=row_index, column=col + 1).fill = PatternFill("solid", fgColor=color)

    last_column_letter = get_column_letter(mcol)
    sheet.auto_filter.ref = f"A1:{last_column_letter}{row_index + 1}"
    sheet.freeze_panes = "C2"

    print(f"Writing: {outfile}")
    wb.save(filename=outfile)
    print(f"{status_histogram=}")


def section_sort_fn(a):
    if a.startswith("SVE"):
        return f"3 {a}"
    if a.startswith("ASIMD"):
        return f"2 {a}"
    return f"1 {a}"


def write_doc_latex_movprfx(outfile, movprfx_table):
    """Write movprfx instruction table to a latex file"""
    all_names = set()
    for names in movprfx_table.values():
        all_names = all_names | names
    rows = []
    for name in sorted(all_names):
        row = [name]
        # either in unpredicated table or in predicated table, either way allowed to use unpredicated movprfx
        row.append("YES")
        row.append("YES" if name in movprfx_table["predicated"] else "NA")
        rows.append(row)

    caption = latex_table.latex_fix("Instructions fusable with MOVPRFX")
    header = ["Instruction", "Unpredicated movprfx", "Predicated movprfx"]
    print(f"Writing: {outfile}")
    with open(outfile, "w") as fout:
        print(latex_table.longtable(rows, header, caption, [None, None, None]), file=fout)


def write_doc_latex(outfile, header, section_info):
    """Write all the data to a latex file"""
    print(f"Writing: {outfile}")
    with open(outfile, "w") as fout:
        for section in sorted(section_info, key=section_sort_fn):
            # Print the groups within this section
            rows = []
            for group in sorted(section_info[section]):
                if "post-increment" in group:
                    continue

                # Second column is the list of instruction mnemonics
                for info, instr_sg in section_info[section][group].items():
                    row = []
                    # First column is the group
                    group_latex = re.sub(r"[&]", "\\&", group)
                    row.append(group_latex)

                    instr = ", ".join(inst for inst in sorted(instr_sg))
                    row.append(instr)

                    # Remaining columns come in order from the tuple constructed in read_xls
                    for field in info:
                        if field:
                            row.append(field)
                        else:
                            row.append("")
                    rows.append(row)
            caption = latex_table.latex_fix(section)
            print("\\newpage", file=fout)
            print(
                latex_table.longtable(rows, header, caption, ["1in", "1in", None, None, None, "1in", None, "1in"]),
                file=fout,
            )


def write_doc_xls(outfile, fields=None, section_info=None):
    """Write all the data to an xls file"""
    wb = openpyxl.Workbook()
    wb.remove(wb["Sheet"])
    sheet = wb.create_sheet("Instruction info")

    row_index = 1
    thin_border = Border(
        left=Side(style="thin"), right=Side(style="thin"), top=Side(style="thin"), bottom=Side(style="thin")
    )

    for section in sorted(section_info, key=section_sort_fn):
        # Print the section title in large bold
        sheet.cell(row=row_index, column=1).value = section
        sheet.cell(row=row_index, column=1).font = Font(name="Calibri", bold=True, size=16)
        row_index += 1

        # Print the column headings in bold
        for column_index, field in enumerate(fields, 1):
            sheet.cell(row=row_index, column=column_index).value = field
            sheet.cell(row=row_index, column=column_index).font = Font(name="Calibri", bold=True, size=10)
        row_index += 1

        # Print the groups within this section
        for group in sorted(section_info[section]):
            if "post-increment" in group:
                continue

            for info, instr in section_info[section][group].items():
                # First column is the group
                sheet.cell(row=row_index, column=1).value = group
                sheet.cell(row=row_index, column=1).border = thin_border

                # Second column is the list of instruction mnemonics
                sheet.cell(row=row_index, column=2).value = ", ".join(inst for inst in sorted(instr))
                sheet.cell(row=row_index, column=2).border = thin_border

                # Remaining columns come in order from the tuple constructed in read_xls
                for column_index, field in enumerate(info, 3):
                    sheet.cell(row=row_index, column=column_index).value = field
                    sheet.cell(row=row_index, column=column_index).border = thin_border
                row_index += 1

        grey = "00AAAAAA"  # grey seperator row
        for col in range(7):
            sheet.cell(row=row_index, column=col + 1).fill = PatternFill("solid", fgColor=grey)
        row_index += 1

    print(f"Writing: {outfile}")
    wb.save(filename=outfile)


# Exclude lots of things which we don't want reported in the table
def skip_instruction(trans):
    # Skip all fusion decodes
    if isinstance(trans, ampere.xlat.Fusion):
        return True

    # Skip IDR-executed instructions
    if len(trans.uops) == 1 and trans.uops[0].unit == "idr":
        return True

    # Skip a list of specific instructions
    mnemonic = trans.asmtemplate.split()[0]
    if mnemonic in excluded_mnemonics:
        return True

    # Skip some groups of instructions based on what the mnemonic starts with
    for e in excluded_starts:
        if mnemonic.startswith(e):
            return True

    # Skip anything which is atomic or exclusive
    for u in trans.uops:
        if hasattr(u, "arm_group"):
            if u.arm_group in ["atomic", "exclusive"]:
                return True

    return False


# Get pipes a uop can execute on, but rename C/D into L/S (load/store)
def get_pipes(uop):
    if uop.unit == "idr":
        return ""
    pipes = "".join(uop.pipe)

    if pipes == "C":
        pipes = "L"
    elif pipes == "D":
        pipes = "S"

    return pipes


# Get sources and dest, accounting for the tricky src_inc/dest_inc fields which change logical register
# 'reg' is expected to be one of {src1, src2, src3, dest}
def get_reg(uop, reg):
    reg_inc = f"{reg}_inc"
    if hasattr(uop, reg):
        if getattr(uop, reg_inc, 0) != 0:
            return f"{getattr(uop, reg)}+{getattr(uop, reg_inc)}"
        return f"{getattr(uop, reg)}"
    return f"no_{reg}"


def get_fsu_unit(uop):
    if hasattr(uop, "fsu_unit"):
        fsu_unit = fsu_unit_map[uop.fsu_unit]
    else:
        fsu_unit = ""

    # Store-data is technically a BY uop, but that's not useful since it's not feeding a consumer uop
    if get_pipes(uop) == "Z":
        fsu_unit = ""

    return fsu_unit


# Get the throughput of an instruction -- the rate at which we could execute them back-to-back.
def get_instr_tpt(uops):
    # First count how many uops are executing on each pipe
    pipe_count = {}
    for pipe in pipe_tpt_map:
        pipe_count[pipe] = 0
    for u in uops:
        pipes = get_pipes(u)
        try:
            pipe_count[pipes] += 1
        except:
            print(f"WARNING: Ignoring missing key {pipes=} for {u.name}")
        if pipes in ["A", "B"]:
            pipe_count["AB"] += 1
        elif pipes in ["X", "Y"]:
            pipe_count["XY"] += 1

    # Then compute how many cycles it would take to get all the uops through each pipe, and take the minimum
    min_tpt = 999
    for pipe, count in pipe_count.items():
        if count:
            min_tpt = min(min_tpt, pipe_tpt_map[pipe] / count)

    # Print fractional throughputs more nicely
    if min_tpt != int(min_tpt):
        if 1 / min_tpt == int(1 / min_tpt):
            return "1/{}".format(int(1 / min_tpt))
        if 2 / min_tpt == int(2 / min_tpt):
            return "2/{}".format(int(2 / min_tpt))
        print(f"Error: throughput {min_tpt}")

    return str(int(min_tpt))


def get_uop_latency(uop, core):
    # Integer loads are 4 cycles, and vector loads are 5 cycles
    if uop.unit == "ld":
        offset = 1 if core in ["trinity", "connor"] else 0
        if uop.dest_type == "gpr":
            return 4 - offset
        if uop.dest_type == "vec":
            return 5 - offset
        if uop.dest_type == "none":
            return 1  # prefetches?
        print(f"Error: unknown dest type {uop.dest_type}")

    # int->fp and fp->int moves have an extra 2 cycles of travel time
    elif "movi2f" in uop.name:
        return uop.latency + 2
    elif "gfmov" in uop.name:
        return uop.latency + 2

    # Some FSU units have a lower internal or unrounded bypass latency
    # than what is specified in the yaml
    elif uop.unit == "fsu":
        if core in ["connor"]:
            return connor_fsu_latency(uop)
        elif core in ["trinity"]:
            return trinity_fsu_latency(uop)
        fsu_unit = get_fsu_unit(uop)
        if fsu_unit == "MA":
            if core in ["furiosa", "ripley"] and uop.DestInhUnrndByp == "Y":
                return 6
            return 5
        elif fsu_unit == "HP":
            return 4
        elif fsu_unit == "MU":
            return 3
        elif fsu_unit == "BU":
            return 2
        elif fsu_unit == "CU":
            if uop.latency == 3:
                return 2
            elif uop.latency == 5:
                return 4
        elif fsu_unit == "SI":
            # On Ripley, SI 3-cycle uops have a 2-cycle local bypass
            if core == "ripley" and uop.latency == 3:
                return 2

    return uop.latency


_FSU_UNIT_LATENCY_PATTERN = re.compile(r"(?P<unit>[A-Za-z]+)(?P<latency>[\d]+)")


def connor_fsu_latency(uop) -> int:  # FIXME: not customized for connor yet.
    if not hasattr(uop, "Producing_Uop"):
        print(f"No producing uop field on {uop.name}")
        sys.exit(1)

    producing_uop = uop.Producing_Uop
    unit = None
    latency = None
    try:
        m = _FSU_UNIT_LATENCY_PATTERN.match(producing_uop)
    except:
        raise ValueError(f"Bad string for fsu producing uop {uop.name}: [{producing_uop}]")
    if m:
        unit = m.group("unit")
        latency = int(m.group("latency"))
    else:
        unit = producing_uop

    # special cases
    if uop.fsu_unit == "Mu" and producing_uop == "Si3" and uop.frc3latency == "Y":
        # things that transition Mu->Si3. These were getting caught in the Si processing below.
        latency = 3
    elif unit == "Si" and latency == 3:
        latency = 2
    elif unit == "Si" and latency == 5:
        latency = 6
    elif unit == "Ma" and latency == 5:
        latency = 4
    elif unit == "Cu" and latency == 5:
        latency = 4
    elif unit == "Cu" and latency == 3:
        latency = 2
    elif unit == "Bu":
        if "dot" in uop.name:
            latency = 2  # presumes late-accumualte dependent chain
        else:
            latency = 3  # mmla  # presumes late-accumualte dependent chain
    elif unit == "Mu":
        latency = 3  # presumes late-accumualte dependent chain
    elif unit == "By":
        latency = 2
    elif unit == "Dv":
        # fix the incorrect yaml latencies for div
        if producing_uop == "Dv29":
            latency = 64 // 4 + 3
        elif producing_uop == "Dv20":
            latency = 32 // 4 + 3
    assert latency is not None
    assert latency > 0
    return latency


def trinity_fsu_latency(uop) -> int:
    if not hasattr(uop, "Producing_Uop"):
        print(f"No producing uop field on {uop.name}")
        sys.exit(1)

    producing_uop = uop.Producing_Uop
    unit = None
    latency = None
    try:
        m = _FSU_UNIT_LATENCY_PATTERN.match(producing_uop)
    except:
        raise ValueError(f"Bad string for fsu producing uop {uop.name}: [{producing_uop}]")
    if m:
        unit = m.group("unit")
        latency = int(m.group("latency"))
    else:
        unit = producing_uop

    # special cases
    if uop.fsu_unit == "Mu" and producing_uop == "Si3" and uop.frc3latency == "Y":
        # things that transition Mu->Si3. These were getting caught in the Si processing below.
        latency = 3
    elif unit == "Si" and latency == 3:
        latency = 2
    elif unit == "Si" and latency == 5:
        latency = 6
    elif unit == "Ma" and latency == 5:
        latency = 4
    elif unit == "Cu" and latency == 5:
        latency = 4
    elif unit == "Cu" and latency == 3:
        latency = 2
    elif unit == "Bu":
        if "dot" in uop.name:
            latency = 2  # presumes late-accumualte dependent chain
        else:
            latency = 3  # mmla  # presumes late-accumualte dependent chain
    elif unit == "Mu":
        latency = 3  # presumes late-accumualte dependent chain
    elif unit == "By":
        latency = 2
    elif unit == "Dv":
        # fix the incorrect yaml latencies for div
        if producing_uop == "Dv29":
            latency = 64 // 4 + 3
        elif producing_uop == "Dv20":
            latency = 32 // 4 + 3
    assert latency is not None
    assert latency > 0
    return latency


def get_instr_latency(mnemonic, uops, instr_fsu_unit, core):
    written = {}
    depends = [set() for _ in range(len(uops))]

    # First pass through the uops to identify dependencies
    # depends will have, for each uop, a list of the uops it depends on
    for ii, uop in enumerate(uops):
        src1 = get_reg(uop, "src1")
        src2 = get_reg(uop, "src2")
        src3 = get_reg(uop, "src3")
        if src1 in written:
            depends[ii].add(written[src1])
        if src2 in written:
            depends[ii].add(written[src2])
        if src3 in written:
            depends[ii].add(written[src3])
        written[get_reg(uop, "dest")] = ii

    done = 0
    cycle = 0
    wrote_back = [False] * len(uops)
    wb_time = [-1] * len(uops)

    # Go cycle by cycle until all uops are done. In each cycle we start execution of all the uops that we
    # can, subject to the constraints of having sources ready and not using more execution pipes than exist.
    while done < len(uops):
        # This tracks the number of uops we have executed on each pipe in this cycle
        cycle_tpt = pipe_tpt_map.copy()

        for ii, uop in enumerate(uops):
            # This uop is already complete
            if wrote_back[ii]:
                continue

            # This uop is completing now
            if wb_time[ii] == cycle:
                wrote_back[ii] = True
                done += 1
                continue

            # This uop is currently executing (WB time in the future)
            if cycle < wb_time[ii]:
                continue

            # See if this uop can start execution now
            # First check whether it depends on anything which has not yet written back
            can_execute = True
            for d in depends[ii]:
                if not wrote_back[d]:
                    can_execute = False
            if not can_execute:
                continue

            # Now see whether we have already used all the pipes needed by this uop in this cycle
            uop_pipe = get_pipes(uop)

            # Uops which are only A or B, or only X or Y, count against their specific pipe and also
            # the overall count of AB or XY uops
            if uop_pipe in ["A", "B"]:
                uop_pipe2 = "AB"
            elif uop_pipe in ["X", "Y"]:
                uop_pipe2 = "XY"
            else:
                uop_pipe2 = None

            # Check whether the pipes this uops can execute on have all been used this cycle
            if uop_pipe == "":
                pass
            elif cycle_tpt[uop_pipe] == 0:
                continue
            else:
                cycle_tpt[uop_pipe] -= 1
            if uop_pipe2:
                if cycle_tpt[uop_pipe2] == 0:
                    continue
                else:
                    cycle_tpt[uop_pipe2] -= 1

            # Passed all checks -- this uop can execute. Schedule its writeback.
            wb_time[ii] = cycle + get_uop_latency(uop, core)

        # Done executing all the uops we can this cycle, move to the next cycle
        cycle += 1

    # This is a quick-n-dirty accounting for cross-unit FP bypassing, to avoid replicating the entire
    # bypass table here. There are only a handful of instructions which contain uops from multiple FP units,
    # so we just handle those specific cases here.
    if instr_fsu_unit == "SI,BY" and len(uops) == 2 and uops[0].latency == 2:
        cycle += 1
    elif instr_fsu_unit == "SI,BY" and len(uops) == 2 and uops[0].latency == 3:
        if core in ["ripley", "trinity", "connor"]:
            cycle += 2
        else:
            cycle += 1
    elif instr_fsu_unit == "MA,BY" and len(uops) == 2:
        cycle += 3  # Ma3 is +3 for Trinity, Ma5 is +2 for Trinity. Check if any Ma5 use this...FIXME
    elif "," in instr_fsu_unit:
        sys.exit(f"Unknown FP bypassing case {instr_fsu_unit}, {len(uops)=} {uops[0].latency=}")

    # We increment cycle one last time after the last uop completes, so subtract 1 from the return value
    return cycle - 1


# If the instruction is one uop, or its first and last uops are in the same unit, just return that unit.
# If the first and last units are different, return them both
def get_instr_fsu_unit(uops):
    if len(uops) == 1:
        return get_fsu_unit(uops[0])
    units = [get_fsu_unit(u) for u in uops]
    units = [u for u in units if u != ""]
    if not units:
        return ""
    if units[0] == units[-1]:
        return units[0]
    return f"{units[0]},{units[-1]}"


if __name__ == "__main__":
    main()
