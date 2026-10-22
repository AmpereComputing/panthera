//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_BRANCH_STREAM_H
#define AMPERE_BRANCH_STREAM_H

#include "ampere/protobuf_utils/protobuf_stream.h"

#include "ampere/instruction_stream/instruction_stream.pb.h"
#include "ampere/branch_stream/branch_stream.pb.h"

namespace ampere::bs {

class BranchStreamWriter : public ProtobufStreamWriter
{
public:
    BranchStreamWriter(boost::filesystem::path filepath);
    bool write(const Branch& branch);
    bool write(const is::Context& context);
    bool write(const is::SysReg& sysreg);
    bool write(const Event& event) { return ProtobufStreamWriter::write(event); }
};

class BranchStreamReader : public ProtobufStreamReader
{
public:
    BranchStreamReader(boost::filesystem::path filepath);
    bool read(Branch& branch);
    bool read(Event& event) { return ProtobufStreamReader::read(event); }
};

} // namespace ampere::bs

#endif // AMPERE_BRANCH_STREAM_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
