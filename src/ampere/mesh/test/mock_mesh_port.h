//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_MESH_TEST_MOCK_MESH_PORT_H
#define AMPERE_MESH_TEST_MOCK_MESH_PORT_H

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ampere/mesh/mesh_port.h"
#include "ampere/sc/testing/test_interfaces.h"

namespace ampere::mesh {

using namespace ::testing;

struct MockMeshPort
{
    struct
    {
        NiceMock<MockTakerWithCredit<MeshReq>> req;
        NiceMock<MockTakerWithCredit<MeshRsp>> rsp;
        NiceMock<MockTakerWithCredit<MeshRsp>> rsp1;
        NiceMock<MockTakerWithCredit<MeshSnp>> snp;
        NiceMock<MockTakerWithCredit<MeshDat>> dat;
    } tx;

    struct
    {
        NiceMock<MockPutter<MeshReq>> req;
        NiceMock<MockPutter<MeshRsp>> rsp;
        NiceMock<MockPutter<MeshRsp>> rsp1;
        NiceMock<MockPutter<MeshSnp>> snp;
        NiceMock<MockPutter<MeshDat>> dat;
    } rx;
};

MATCHER_P(HasOpcode, opcode, "")
{
    if (arg->Opcode == opcode)
        return true;

    *result_listener << "opcode mismatch: expected " << opcode << " got " << arg->Opcode;
    return false;
}

MATCHER_P(HasAddress, addr, "")
{
    if (arg->Addr == addr)
        return true;

    *result_listener << "address mismatch: expected " << addr << " got " << arg->Addr;
    return false;
}

MATCHER_P(HasTxnID, txnid, "")
{
    if (arg->TxnID == txnid)
        return true;

    *result_listener << "TxnID mismatch: expected " << txnid << " got " << arg->TxnID;
    return false;
}

MATCHER_P(HasLikelyShared, value, "")
{
    if (arg->LikelyShared == value)
        return true;

    *result_listener << "LikelyShared mismatch: expected " << value;
    return false;
}

MATCHER_P(HasAllowRetry, value, "")
{
    if (arg->AllowRetry == value)
        return true;

    *result_listener << "AllowRetry mismatch: expected " << value;
    return false;
}

MATCHER_P(HasPassDirty, value, "")
{
    if (arg->Resp.pass_dirty() == value)
        return true;

    *result_listener << "PassDirty mismatch: expected " << value;
    return false;
}

MATCHER_P(HasDatSource, value, "")
{
    if (arg->DatSource == value)
        return true;

    *result_listener << "DatSource mismatch: expected " << value << " got " << arg->DatSource;
    return false;
}

} // namespace ampere::mesh
#endif // AMPERE_MESH_TEST_MOCK_MESH_PORT_H
