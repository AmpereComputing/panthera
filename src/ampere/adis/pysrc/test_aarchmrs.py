#!/usr/bin/env python3
#
# Copyright (c) 2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
"""Regression checks for the vendored AARCHMRS input."""

from __future__ import annotations

import argparse
import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path

import aarchmrs

AARCHMRS_SOURCE = Path(__file__).parents[1] / "data/aarchmrs-2026-09/Instructions.json.gz"


class AarchmrsTest(unittest.TestCase):
    def test_a64_is_complete_and_well_formed(self) -> None:
        encodings = aarchmrs.read_a64(AARCHMRS_SOURCE)

        self.assertEqual(4429, len(encodings))
        self.assertEqual(4429, len({encoding.name for encoding in encodings}))
        self.assertIn("ADD_32_addsub_imm", {encoding.name for encoding in encodings})

        branch = next(encoding for encoding in encodings if encoding.name == "B_only_branch_imm")
        self.assertEqual(("imm26",), branch.encoded_in["<label>"])
        self.assertEqual((), branch.conditions[0])

        conditional = next(encoding for encoding in encodings if encoding.name == "dup_z_r_bhs_")
        self.assertEqual((aarchmrs.Constraint("size", "ne", "11"),), conditional.conditions[0])

        prefetch = next(encoding for encoding in encodings if encoding.name == "PRFM_P_ldst_regoff")
        self.assertEqual((aarchmrs.Constraint("Rt", "ne", "11---"),), prefetch.conditions[0])

    def test_a64_translates_to_adis_records(self) -> None:
        with tempfile.TemporaryDirectory() as directory, contextlib.redirect_stdout(io.StringIO()):
            report = Path(directory) / "unimplemented.json"
            records = aarchmrs.decorated_encodings(AARCHMRS_SOURCE, unimplemented_report=report)
            report_data = json.loads(report.read_text())

        self.assertEqual(3913, len(records))
        self.assertEqual(3913, len(report_data["implemented"]))
        self.assertEqual(5231, len(report_data["implemented"]) + len(report_data["unimplemented"]))
        self.assertEqual(aarchmrs._implemented_names(AARCHMRS_SOURCE), set(report_data["implemented"]))

        by_name = {record.get_encoding_name(): record for record in records}
        branch = by_name["B_only_branch_imm"]
        self.assertEqual("B", branch.get_mnemonic())
        self.assertEqual("000101--------------------------", branch.get_rexp())
        self.assertTrue(branch.is_branch())
        self.assertFalse(branch.is_branch_cond())

        conditional_branch = by_name["B_only_condbranch"]
        self.assertTrue(conditional_branch.is_branch())
        self.assertTrue(conditional_branch.is_branch_cond())
        self.assertTrue(conditional_branch.may_read_flags())
        self.assertFalse(by_name["CBZ_64_compbranch"].may_read_flags())
        self.assertFalse(by_name["TBL_asimdtbl_L1_1"].is_branch())

        self.assertTrue(by_name["LDR_64_ldst_immpre"].pre_index())
        self.assertTrue(by_name["LDR_64_ldst_immpost"].post_index())
        self.assertTrue(by_name["CPYFP_CPY_memcms"].post_index())
        self.assertTrue(by_name["SETGP_SET_memcms"].pre_index())

        gather = by_name["ldff1d_z_p_bz_d_64_scaled"]
        self.assertTrue(gather.is_gather())
        self.assertTrue(gather.is_load_ff())
        self.assertFalse(gather.is_atomic())
        self.assertEqual(64, gather.get_memop_esize())
        self.assertEqual(64, gather.get_memop_msize())
        self.assertTrue(gather.predicated())

        self.assertEqual("sve2p1", by_name["st1w_z_p_br_u128"].instruction_class())
        self.assertTrue(by_name["st1w_z_p_br_u128"].is_sve_family())
        self.assertTrue(by_name["ucvtf_z_p_z_x2dz"].is_sve_family())

        # Panthera uses this count while resolving MOVPRFX fusion.  It must
        # count data-vector operands, not vector registers used only as an
        # address or element index.
        self.assertEqual(3, by_name["add_z_p_zz_"].get_vector_srcs())
        self.assertEqual(0, by_name["ldff1d_z_p_bz_d_64_scaled"].get_vector_srcs())
        self.assertEqual(3, by_name["st3w_z_p_bi_contiguous"].get_vector_srcs())
        self.assertEqual(1, by_name["luti4_z_zz_2x16"].get_vector_srcs())
        self.assertEqual(2, by_name["pmlal_mz_zzzw_1x2"].get_vector_srcs())

        def matches(pattern: str, instruction: int) -> bool:
            bits = f"{instruction:032b}"
            return all(expected == "-" or expected == actual for expected, actual in zip(pattern, bits))

        # System-operation rules carry the low-order selection bits while
        # their alias carries the rest of the field constraint.  Verify that
        # both are combined before decoder patterns are emitted.
        self.assertTrue(
            any(matches(pattern, 0xD5087800) for pattern in by_name["SYS_CR_systeminstrs_AT_S1E1R"].json_patterns)
        )
        self.assertFalse(
            any(matches(pattern, 0xD5087800) for pattern in by_name["SYS_CR_systeminstrs_AT_S1E1RP"].json_patterns)
        )
        self.assertTrue(
            any(matches(pattern, 0xD50E7800) for pattern in by_name["SYS_CR_systeminstrs_AT_S1E3R"].json_patterns)
        )

        self.assertTrue(any(matches(pattern, 0x33040121) for pattern in by_name["BFM_32M_bitfield"].json_patterns))
        self.assertFalse(any(matches(pattern, 0x33240121) for pattern in by_name["BFM_32M_bitfield"].json_patterns))
        self.assertTrue(any(matches(pattern, 0x082A7F7C) for pattern in by_name["CASP_CP32_comswappr"].json_patterns))
        self.assertFalse(any(matches(pattern, 0x082A7F7D) for pattern in by_name["CASP_CP32_comswappr"].json_patterns))

    def test_assembly_rules_preserve_operand_semantics(self) -> None:
        rules = aarchmrs._load_document(AARCHMRS_SOURCE)["assembly_rules"]
        records = {record.name: record for record in aarchmrs.read_a64(AARCHMRS_SOURCE)}

        branch, branch_explanations = aarchmrs._make_assembly(records["B_only_condbranch"], rules)
        self.assertEqual("B.<cond>  <label>", branch.get_prototype())
        self.assertEqual(["B", "<cond>", "  ", "<label>"], [operand.get_text() for operand in branch.get_operands()])
        condition_table = branch_explanations["operand_2"].get_table()
        self.assertIsNotNone(condition_table)
        self.assertEqual("eq", condition_table.row_dict["0000"])
        self.assertEqual("cc", condition_table.row_dict["0011"])

        add, add_explanations = aarchmrs._make_assembly(records["ADD_64_addsub_imm"], rules)
        self.assertEqual("ADD  <Xd|SP>,<Xn|SP>,#<imm>", add.get_prototype())
        self.assertIsNone(add_explanations["operand_2"].get_table())
        self.assertIsNone(add_explanations["operand_4"].get_table())
        self.assertIsNone(add_explanations["operand_7"].get_table())

        add_shift, add_shift_explanations = aarchmrs._make_assembly(records["ADD_64_addsub_shift"], rules)
        self.assertEqual("ADD  <Xd>,<Xn>,<Xm>,<shift> #<amount>", add_shift.get_prototype())
        self.assertIsNone(add_shift_explanations["operand_2"].get_table())
        self.assertIsNone(add_shift_explanations["operand_4"].get_table())
        self.assertIsNone(add_shift_explanations["operand_6"].get_table())

        add_extended, add_extended_explanations = aarchmrs._make_assembly(records["ADD_64_addsub_ext"], rules)
        self.assertEqual("ADD  <Xd|SP>,<Xn|SP>,<R><m>,<extend> #<amount>", add_extended.get_prototype())
        self.assertIsNone(add_extended_explanations["operand_6"].get_table())


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=None)
    args, remaining = parser.parse_known_args()
    if args.source:
        # Kept as a command-line hook for input-release validation.  The unit
        # test itself intentionally checks the vendored source location.
        aarchmrs.read_a64(args.source)
    unittest.main(argv=[__file__, *remaining])
