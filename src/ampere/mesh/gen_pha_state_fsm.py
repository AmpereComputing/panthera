#!/usr/bin/env python3
# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Generate the PHA FSM for managing cache state transitions.
"""

import argparse
import logging
import os
import sys

import pandas as pd

log = logging.getLogger(__name__)
PROJECT_PATH = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def main():
    args = parse_args()
    if args.output:
        out = open(args.output, "w")
    else:
        out = sys.stdout
    excel_to_cpp(args.fsm, out)
    out.close()


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("fsm", help="Input FSM file (xlsx)")
    parser.add_argument("-o", "--output", help="Output file")
    return parser.parse_args()


class Fsm:
    SPECIALS = str.maketrans({" ": "_", ".": "_"})

    OPCODE_RENAMES = {
        "LcrdReturn": "ReqLCrdReturn",
        "SnpRespData_*_Fwded_*": "SnpRespDataFwded",
        "SnpRespData_*_PD_Fwded_*": "SnpRespDataFwded",
        "SnpResp_PD_*_Fwded_*": "SnpRespDataFwded",
        "SnpRespData_*": "SnpRespData",
        "SnpRespData_*_Read": "SnpRespData",
        "SnpRespData_I_Read": "SnpRespData",
        "CBWrData": "CopyBackWrData",
        "CBWrData_*": "CopyBackWrData",
        "CompData_*": "CompData",
        "CopyBackWrData_*": "CopyBackWrData",
        "NCBWrData": "NonCopyBackWrData",
        "Comp_*": "Comp",
        "SnpResp_*_Fwded_*": "SnpRespFwded",
        "SnpResp_*": "SnpResp",
        "SnpResp_*_Read": "SnpResp",  # This indicates SnpResp with DataPull (which is actually a separate field)
        "SnpResp_I": "SnpResp",
        "SnpResp_SC": "SnpResp",
        "SnpResp_I_Read": "SnpResp",
        "RespSepData_*": "RespSepData",
        "AtomicLoad": "AtomicLoad_ADD|AtomicLoad_CLR|AtomicLoad_EOR|AtomicLoad_SET|AtomicLoad_SMAX|AtomicLoad_SMIN|AtomicLoad_UMAX|AtomicLoad_UMIN",
        "AtomicStore": "AtomicStore_ADD|AtomicStore_CLR|AtomicStore_EOR|AtomicStore_SET|AtomicStore_SMAX|AtomicStore_SMIN|AtomicStore_UMAX|AtomicStore_UMIN",
    }

    def __init__(self, xlsx_file, sheet_name):
        self.df = self._read_table(xlsx_file, sheet_name)

    def set_transition_vars(self, sno, transition):
        pass

    def gen_transitions(self, out):
        self.gen_transitions_from_table(self.df, out)

    def gen_transitions_from_table(self, df, out, must_match=False, ignore_input=None):
        if ignore_input is None:
            ignore_input = set()

        for sno, transition in df.iterrows():
            self.set_transition_vars(sno, transition)

            print(f"    // Row {int(sno)}", file=out)
            print(f"    if ({self._input_match(transition, ignore_input)}) {{", file=out)
            print(f"        {self._output(transition)}", file=out)
            print(f"        return {int(sno)};", file=out)
            print("    }", file=out)

    def _read_table(self, xlsx_file, sheet_name):
        df = pd.read_excel(xlsx_file, sheet_name=sheet_name, engine="openpyxl", keep_default_na=False)
        df.drop(df.filter(regex="Unnamed"), axis=1, inplace=True)

        self.inputs = []
        self.outputs = []

        for col_name, col_type in df.iloc[0].items():
            if col_type == "SI":
                self.inputs.append(col_name)
            elif col_type == "SO":
                self.outputs.append(col_name)

        def rename_opcode(value):
            if value in self.OPCODE_RENAMES:
                return self.OPCODE_RENAMES[value]
            else:
                return value

        if "Opcode" in df.columns:
            df["Opcode"] = df["Opcode"].apply(rename_opcode)

        if "Tx Channel Opcode" in df.columns:
            df["Tx Channel Opcode"] = df["Tx Channel Opcode"].apply(rename_opcode)

        if "CHIRP Opcode" in df.columns:
            # Derive the DataPull column from the annotated CHIRP Opcode name.
            # Why do we do this to ourselves???
            def is_snp_resp_read(op):
                return op in {"SnpResp_I_Read", "SnpRespData_I_Read", "SnpResp_*_Read"}

            df["DataPull"] = df["CHIRP Opcode"].apply(is_snp_resp_read)
            df["CHIRP Opcode"] = df["CHIRP Opcode"].apply(rename_opcode)

        df = df[df["S.No"] != ""]
        df["S.No"] = df["S.No"].astype(int)
        return df.set_index("S.No")

    def _input_match(self, transition, ignore):
        cmps = []

        for col in self.inputs:
            if col in ignore:
                continue

            value = transition[col]

            if type(value) is str and value[0] == "x":
                continue

            xform = getattr(self, col.translate(self.SPECIALS), None)
            if callable(xform):
                cmps.append(xform(value))
            elif xform is None:
                cmps.append(f"entry.{col} == {value}")
            else:
                log.fatal(f"don't know how to match column {col}")

        return " && ".join([f"({cmp})" for cmp in cmps])

    def _output(self, transition):
        outs = []

        for col in self.outputs:
            value = transition[col]

            if type(value) is str and value[0] == "x":
                continue

            xform = getattr(self, col.translate(self.SPECIALS), None)
            if callable(xform):
                xformed = xform(value)
                if type(xformed) is list:
                    outs += xformed
                else:
                    outs.append(xformed)
            elif xform is None:
                outs.append(f"entry.{col} = {value};")

        return "\n        ".join(outs)

    def Phat_PST_Update(self, pst_text):
        result = []
        pst = pst_text.lower()
        if pst == "pst = requester":
            result.append("entry.pst_opcode = PstUpdateOpcode::PST_IS_NODEID;")
            result.append("entry.pst_set_node = entry.request->SrcID;")
        elif pst == "pst = srcid of chirp":
            result.append("entry.pst_opcode = PstUpdateOpcode::PST_IS_NODEID;")
            result.append("entry.pst_set_node = entry.chirp_nid;")
        elif pst == "pst = calculate new pst from stvector":
            result.append("entry.pst_opcode = PstUpdateOpcode::PST_CALC_NEW;")
        elif pst == "none":
            result.append("// No Change entry.pst_opcode = PstUpdateOpcode::PST_NOUPDATE;")
        else:
            raise RuntimeError(f'unrecognized text in PST Update: "{pst_text}"')
        return result

    def Next_PO(self, po):
        if po == "PO":
            return "// No change to entry.input.PO"
        elif po == 0:
            return "entry.input.PO = false;"
        elif po == 1:
            return "entry.input.PO = true;"
        else:
            raise RuntimeError(f'unrecognized text in Next PO: "{po}"')


class ChirpFsm(Fsm):
    def __init__(self, xlsx_file, sheet_name):
        super().__init__(xlsx_file, sheet_name)
        self.inputs.append("DataPull")

    def gen_transitions(self, out, data_channel=False):
        self.data_channel = data_channel
        if self.data_channel:
            self.gen_transitions_from_table(
                self.df[self.df["CHIRP channel"] == "RxDAT"], out, must_match=True, ignore_input={"CHIRP channel"}
            )
        else:
            self.gen_transitions_from_table(
                self.df[self.df["CHIRP channel"] != "RxDAT"], out, must_match=True, ignore_input={"CHIRP channel"}
            )

    def set_transition_vars(self, sno, transition):
        super().set_transition_vars(sno, transition)
        self.chirp_opcode = transition["CHIRP Opcode"]

    def Opcode(self, opcode):
        if "|" in opcode:
            opcodes = opcode.split("|")
            return f"({' || '.join([f'entry.request->Opcode == chi::RequestOpcode::{op}' for op in opcodes])})"
        else:
            return f"entry.request->Opcode == chi::RequestOpcode::{opcode}"

    def CHIRP_Opcode(self, opcode):
        if self.data_channel:
            return f"dat->Opcode == chi::DataOpcode::{opcode}"
        else:
            return f"rsp->Opcode == chi::ResponseOpcode::{opcode}"

    def Phat_CacheState(self, state):
        return f"entry.input.CacheState == chi::FinalState::{state}"

    def Phat_PdbState(self, state):
        return f"entry.pdb_state == chi::FinalState::{state}"

    def Resp_State(self, state):
        pkt = "dat" if self.data_channel else "rsp"
        state = state.replace(" ", "_")
        if self.chirp_opcode.startswith("Snp"):
            return f"{pkt}->Resp == chi::RespSnoop_{state}"
        elif self.chirp_opcode.startswith("Comp") or self.chirp_opcode == "RespSepData":
            return f"{pkt}->Resp == chi::RespComp_{state}"
        elif self.chirp_opcode.startswith("CB") or self.chirp_opcode.startswith("CopyBack"):
            return f"{pkt}->Resp == chi::RespWriteData_{state}"
        else:
            return "true/* the FSM also checks Resp on DataSepResp, but I don't trust it yet */"

    def FwdState(self, state):
        pkt = "dat" if self.data_channel else "rsp"
        state = state.replace(" ", "_")
        return f"{pkt}->FwdState == chi::RespComp_{state}"

    def PO_Or_PmHA(self, value):
        value = int(value)
        if value == 1:
            return "(entry.input.PHA == PHA_t::Pmha || entry.input.PO)"
        elif value == 0:
            return "!(entry.input.PHA == PHA_t::Pmha || entry.input.PO)"
        else:
            log.fatal(f"bad value ({value}) for PO OR PmHA")

    def Gt1Copy(self, value):
        return f"entry.input.Gt1Copy == {value}"

    def RnIsPST(self, value):
        return f"entry.input.RnIsPst == {value}"

    def Next_Phat_CacheState(self, state):
        if state == "Phat.CacheState":
            return "// no change to entry.input.CacheState"
        elif state == "Resp":
            if self.data_channel:
                return "entry.input.CacheState = dat->Resp.state();"
            else:
                return "entry.input.CacheState = rsp->Resp.state();"
        elif state.startswith("Phat.Pdb"):
            return "entry.input.CacheState = entry.pdb_state;"
        elif state.startswith("Phat.Adb"):
            return 'AMPERE_FATAL("Not adb_state Supported // entry.input.CacheState = entry.adb_state;");'
        else:
            return f"entry.input.CacheState = chi::FinalState::{state};"

    def Next_Phat_PdbState(self, state):
        if state == "Phat.PdbState":
            return "// no change to entry.PdbState"
        elif state == "Resp":
            if self.data_channel:
                return "entry.pdb_state = dat->Resp.state();"
            else:
                return "entry.pdb_state = rsp->Resp.state();"
        else:
            return f"entry.pdb_state = chi::FinalState::{state};"

    def StVecUpdate(self, update_text):
        result = []
        lines = update_text.lower().split("\n")
        do_set = False
        do_clear = False
        for line in lines:
            line = line.strip()
            if not line:
                continue
            if line == "set requester" or line == "set bit corresponding to requester":
                result.append("entry.stvec_set_node = entry.request->SrcID;")
                do_set = True
            elif (
                line == "set bit corresponding to srcid of chirp"
                or line == "set bit corresponding to stashee(srcid ) of chirp"
                or line == "set bit corressponding to srcid of chirp"
            ):
                result.append("entry.stvec_set_node = entry.chirp_nid;")
                do_set = True
            elif line == "clear bit for requester in stvector" or line == "clear bit corresponding to requester":
                result.append("entry.stvec_clear_node = entry.request->SrcID;")
                do_clear = True
            elif line == "clear bit corresponding to srcid of chirp":
                result.append("entry.stvec_clear_node = entry.chirp_nid;")
                do_clear = True
            elif line == "none":
                pass
            else:
                raise RuntimeError(f'unrecognized text in StVecUpdate: "{update_text}"')
        if do_set and do_clear:
            result.append("entry.stvec_opcode = StVecUpdateOpcode::STVEC_SET_AND_CLEAR;")
        elif do_set:
            result.append("entry.stvec_opcode = StVecUpdateOpcode::STVEC_SET_ONLY;")
        elif do_clear:
            result.append("entry.stvec_opcode = StVecUpdateOpcode::STVEC_CLEAR_ONLY;")
        else:
            pass
        return result

    def DataPull(self, val):
        pkt = "dat" if self.data_channel else "rsp"
        if val:
            return f"{pkt}->DatPull == DataPull::Read"
        else:
            return f"{pkt}->DatPull == DataPull::NoRead"


class PipeFsm(Fsm):
    def Opcode(self, opcode):
        if "|" in opcode:
            opcodes = opcode.split("|")
            return f"({' || '.join([f'entry.request->Opcode == chi::RequestOpcode::{op}' for op in opcodes])})"
        else:
            return f"entry.request->Opcode == chi::RequestOpcode::{opcode}"

    def Tx_Channel(self, chan):
        if chan == "TxSNP":
            self.chan_opcode = "entry.output.TxSnpOpcode"
            self.chan_opcode_type = "SnoopOpcode"
        elif chan == "TxREQ":
            self.chan_opcode = "entry.output.TxReqOpcode"
            self.chan_opcode_type = "RequestOpcode"
        elif chan == "TxRSP0":
            self.chan_opcode = "entry.output.TxRsp0Opcode"
            self.chan_opcode_type = "ResponseOpcode"
        elif chan == "TxDAT":
            self.chan_opcode = "entry.output.TxDatOpcode"
            self.chan_opcode_type = "DataOpcode"
        else:
            log.fatal(f"unrecognized Tx Channel {chan}")
        return self.chan_opcode

    def IntrPdbValid(self, value):
        return f"entry.input.IntrPdbValid == {value}"

    def IntrAdbValid(self, value):
        return f"entry.input.IntrAdbValid == {value}"

    def PdbState(self, state):
        return f"entry.pdb_state == chi::FinalState::{state}"

    def DatTagOp(self, state):
        return f"entry.DatTagOp == pha_fsm::ReqTagOp_t::{state}"

    def AdbState(self, state):
        return f"true /* entry.adb_state == {state} */"

    def RnMaybeHasCopy(self, value):
        value = bool(value)
        if value:
            return "entry.snoop_status.core_is_valid(entry.request->SrcID)"
        else:
            return "!entry.snoop_status.core_is_valid(entry.request->SrcID)"

    def Phat_CacheState(self, state):
        return f"entry.input.CacheState == chi::FinalState::{state}"

    def Tx_Channel_Opcode(self, opcode):
        if "|" in opcode:
            opcodes = opcode.split("|")
            return f"({' || '.join([f'*{self.chan_opcode} == chi::{self.chan_opcode_type}::{opcode}' for opcode in opcodes])})"
        else:
            return f"*{self.chan_opcode} == chi::{self.chan_opcode_type}::{opcode}"

    def PO_Or_PmHA(self, value):
        value = int(value)
        if value == 1:
            return "(entry.input.PHA == PHA_t::Pmha || entry.input.PO)"
        elif value == 0:
            return "!(entry.input.PHA == PHA_t::Pmha || entry.input.PO)"
        else:
            log.fatal(f"bad value ({value}) for PO OR PmHA")

    def Gt1Copy(self, value):
        return f"entry.input.Gt1Copy == {value}"

    def RnIsPST(self, value):
        return f"entry.input.RnIsPst == {value}"

    def Next_Phat_CacheState(self, state):
        if state == "Phat.CacheState":
            return "// no change to entry.input.CacheState"
        else:
            return f"entry.input.CacheState = chi::FinalState::{state};"

    def Next_Phat_PdbState(self, state):
        if state == "Phat.PdbState":
            return "// no change to entry.PdbState"
        else:
            return f"entry.pdb_state = chi::FinalState::{state};"

    def Next_AdbState(self, state):
        if state == "AdbState":
            return "// no change to entry.AdbState"
        else:
            return f"entry.adb_state = pha_fsm::AdbState_t::{state};"

    def StVecUpdate(self, update_text):
        result = []
        lines = update_text.lower().split("\n")
        for line in lines:
            line = line.strip()
            if line == "clear all bits":
                result.append("entry.stvec_opcode = StVecUpdateOpcode::STVEC_CLEAR_ALL;")
            elif line == "clear all bits except requester":
                result.append("entry.stvec_set_node = entry.request->SrcID;")
                result.append("entry.stvec_opcode = StVecUpdateOpcode::STVEC_CLEAR_ALL_EXCEPT_RN;")
            elif line == "clear bit corresponding to requester":
                result.append("entry.stvec_clear_node = entry.request->SrcID;")
                result.append("entry.stvec_opcode = StVecUpdateOpcode::STVEC_CLEAR_ONLY;")
            elif line == "set bit corresponding to requester":
                result.append("entry.stvec_set_node = entry.request->SrcID;")
                result.append("entry.stvec_opcode = StVecUpdateOpcode::STVEC_SET_ONLY;")
            elif line == "none":
                pass
            else:
                raise RuntimeError(f'unrecognized text in StVecUpdate: "{update_text}"')
        return result

    def Next_DatTagOp(self, dattagop):
        if dattagop == "DatTagOp":
            return "// No change to entry.DatTagOp"
        else:
            return f"entry.DatTagOp = pha_fsm::ReqTagOp_t::{dattagop};"


def excel_to_cpp(fsm, out):
    chirp = ChirpFsm(fsm, sheet_name="CacheStateExpandedTableChirp")
    pipe = PipeFsm(fsm, sheet_name="CacheStateExpandedTablePipeline")
    relative_fsm_path = os.path.relpath(fsm, start=PROJECT_PATH)

    print(
        f"""
//
// Generated by gen_state_fsm.py from {relative_fsm_path} - DO NOT EDIT
//
#include "ampere/mesh/chi/types.h"
#include "ampere/mesh/mesh_req.h"
#include "ampere/mesh/mesh_rsp.h"
#include "ampere/mesh/mesh_snp.h"
#include "ampere/mesh/mesh_dat.h"
#include "ampere/mesh/pha_tracker_entry.h"
#include "ampere/mesh/pha_fsm.h"
#include "ampere/mesh/pha_state_fsm.h"

namespace ampere::mesh::pha_fsm {{
""",
        file=out,
    )

    print("int pha_state_fsm_recv_dat(mesh::PHATrackerEntry& entry, MeshDat dat)\n{", file=out)
    chirp.gen_transitions(out, data_channel=True)
    print('    throw PhaCacheStateFsmNoTransition(fmt::format("Unknown CacheState state transition:"));', file=out)
    print("}\n\n", file=out)

    print("int pha_state_fsm_recv_rsp(mesh::PHATrackerEntry& entry, MeshRsp rsp)\n{", file=out)
    chirp.gen_transitions(out)
    print('    throw PhaCacheStateFsmNoTransition(fmt::format("Unknown CacheState state transition:"));', file=out)
    print("}\n\n", file=out)

    print("int pha_state_fsm_pipe(mesh::PHATrackerEntry& entry)\n{", file=out)
    pipe.gen_transitions(out)
    print("    return -1;", file=out)
    print("}\n\n", file=out)

    print("} // namespace ampere::mesh::pha_fsm", file=out)


if __name__ == "__main__":
    main()
