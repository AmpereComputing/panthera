//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "branch_stream.h"

namespace ampere::bs {

const uint32_t protobuf_magic = 0x53424250; // 0x50(P) 0x42(B) 0x42(B) 0x53(S)
const uint32_t protobuf_bs_version = 1;

BranchStreamWriter::BranchStreamWriter(boost::filesystem::path filepath)
    : ProtobufStreamWriter(filepath, protobuf_magic, protobuf_bs_version)
{
}

bool BranchStreamWriter::write(const Branch& branch)
{
    Event event;
    event.mutable_branch()->CopyFrom(branch);
    return write(event);
}

bool BranchStreamWriter::write(const is::Context& context)
{
    Event event;
    event.mutable_context()->CopyFrom(context);
    return write(event);
}

bool BranchStreamWriter::write(const is::SysReg& sysreg)
{
    Event event;
    event.mutable_sysreg()->CopyFrom(sysreg);
    return write(event);
}

BranchStreamReader::BranchStreamReader(boost::filesystem::path filepath)
    : ProtobufStreamReader(filepath, protobuf_magic, protobuf_bs_version)
{
}

bool BranchStreamReader::read(Branch& branch)
{
    Event event;
    bool status = true;
    while (status && !event.has_branch())
        status = read(event);
    if (status) {
        branch.Swap(event.mutable_branch());
    }
    return status;
}

} // namespace ampere::bs

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
