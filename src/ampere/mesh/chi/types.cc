//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <iostream>
#include <signal.h>

#include "ampere/mesh/chi/types.h"

namespace ampere::mesh::chi {

ResponseStatus const RespComp_I = ResponseStatus{ResponseStatus::Type::Comp, 0b000};
ResponseStatus const RespComp_SC = ResponseStatus{ResponseStatus::Type::Comp, 0b001};
ResponseStatus const RespComp_UC = ResponseStatus{ResponseStatus::Type::Comp, 0b010};
ResponseStatus const RespComp_UD_PD = ResponseStatus{ResponseStatus::Type::Comp, 0b110};
ResponseStatus const RespComp_SD_PD = ResponseStatus{ResponseStatus::Type::Comp, 0b111};

ResponseStatus const RespWriteData_I = ResponseStatus{ResponseStatus::Type::WriteData, 0b000};
ResponseStatus const RespWriteData_SC = ResponseStatus{ResponseStatus::Type::WriteData, 0b001};
ResponseStatus const RespWriteData_UC = ResponseStatus{ResponseStatus::Type::WriteData, 0b010};
ResponseStatus const RespWriteData_UD_PD = ResponseStatus{ResponseStatus::Type::WriteData, 0b110};
ResponseStatus const RespWriteData_SD_PD = ResponseStatus{ResponseStatus::Type::WriteData, 0b111};

ResponseStatus const RespSnoop_I = ResponseStatus{ResponseStatus::Type::Snoop, 0b000};
ResponseStatus const RespSnoop_SC = ResponseStatus{ResponseStatus::Type::Snoop, 0b001};
ResponseStatus const RespSnoop_UC = ResponseStatus{ResponseStatus::Type::Snoop, 0b010};
ResponseStatus const RespSnoop_UD = ResponseStatus{ResponseStatus::Type::Snoop, 0b010}; // same as UC
ResponseStatus const RespSnoop_SD = ResponseStatus{ResponseStatus::Type::Snoop, 0b011};
ResponseStatus const RespSnoop_I_PD = ResponseStatus{ResponseStatus::Type::Snoop, 0b100};
ResponseStatus const RespSnoop_SC_PD = ResponseStatus{ResponseStatus::Type::Snoop, 0b101};
ResponseStatus const RespSnoop_UC_PD = ResponseStatus{ResponseStatus::Type::Snoop, 0b110};

Request::Request(NodeId src_id, NodeId tgt_id, uint16_t txn, RequestOpcode opcode, archx::PhysAddr addr)
    : QoS{0},
      TgtID{tgt_id},
      SrcID{src_id},
      LRID{LocalRoutingId{0, 0, 0, 0}},
      LDID{src_id},
      TxnID{txn},
      LPID{0},
      ReturnNID{src_id},
      ReturnTxnID{txn},
      StashNIDValid{false},
      StashLPID{0},
      Opcode{opcode},
      Addr{addr},
      NS{false},
      Size{64},
      AllowRetry{allow_retry(opcode)},
      PCrdType{0},
      ExpCompAck{needs_comp_ack(opcode)},
      MemAttr{0},
      SnpAttr{is_snooping(opcode)},
      SnoopMe{false},
      LikelyShared{false},
      Excl{false},
      Order{RequestOrder::NoOrder},
      Endian{false},
      TraceTag{false},
      RSVDC{}
{
}

Response::Response(NodeId src_id, NodeId tgt_id, uint16_t txn, ResponseOpcode opcode)
    : QoS{0},
      TgtID{tgt_id},
      SrcID{src_id},
      LRID{LocalRoutingId{0, 0, 0, 0}},
      TxnID{txn},
      CBusyS{CBusyState::None},
      DBID{0},
      PCrdType{0},
      Opcode{opcode},
      RespErr{ResponseErr::NormalOkay},
      Resp{},
      FwdState{},
      DatPull{DataPull::NoRead},
      TraceTag{false}
{
}

Snoop::Snoop(NodeId src_id, uint16_t txn, SnoopOpcode opcode, archx::PhysAddr addr)
    : QoS{0},
      TxnID{txn},
      FwdNID{NodeId(NodeId::NodeId_Type::Local)},
      FwdTxnID{0},
      StashLPID{0},
      StashLPIDValid{false},
      VMIDExt{0},
      SrcID{src_id},
      Opcode{opcode},
      Addr{addr},
      NS{false},
      DoNotGoToSD{false},
      DoNotDataPull{false},
      RetToSrc{false},
      TraceTag{false}
{
}

Data::Data(NodeId src_id, NodeId tgt_id, uint16_t txn, DataOpcode opcode)
    : QoS{0},
      TgtID{tgt_id},
      SrcID{src_id},
      LRID{LocalRoutingId{0, 0, 0, 0}},
      TxnID{txn},
      HomeNID{NodeId(NodeId::NodeId_Type::Local)},
      DBID{0},
      Opcode{opcode},
      RespErr{ResponseErr::NormalOkay},
      Resp{},
      FwdState{},
      DatPull{DataPull::NoRead},
      DatSource{DataSource::NotSupported},
      CBusyS{CBusyState::None},
      CCID{0},
      DataID{0},
      BE{0xffffffffffffffffull},
      DataPtr{nullptr},
      DataCheck{false},
      Poison{false},
      TraceTag{false},
      RSVDC{}
{
}

} // namespace ampere::mesh::chi
