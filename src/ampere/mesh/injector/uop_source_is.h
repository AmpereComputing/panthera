//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_MESH_INJECTOR_UOP_SOURCE_IS_H
#define AMPERE_MESH_INJECTOR_UOP_SOURCE_IS_H

#include <limits>
#include <memory>
#include <string>

#include "ampere/archx/driver.h"
#include "ampere/mesh/injector/uop_source.h"

namespace ampere::mesh {

using namespace archx;

class UopSourceIS : public UopSource, poly<UopSourceIS>
{
public:
    explicit UopSourceIS(const sc_core::sc_module_name& name);
    Param<std::string> instruction_stream_name{"is", "path to instruction stream", ""};
    Param<std::vector<std::string>> decoders{
        "decoders", "list of decoder names, in the order they should be checked", "[]"_json};
    std::optional<Uop> fetch_uop() override;
    std::optional<IOop> fetch_io() override;
    std::pair<chi::IOType, uint64_t> peek_io() override;
    std::optional<IOop> fetch_read_io() override;
    std::optional<IOop> fetch_write_io() override;
    void commit_uop(Uop) override;
    uint64_t take_next_id() override { return ++uop_id; }

    void end_of_elaboration() override;

private:
    std::unique_ptr<Driver> driver;

    uint64_t uop_id{0};
    uint64_t last_uop_id{std::numeric_limits<uint64_t>::max()};
    uint64_t instructions{0};
    bool stopped{false};

    std::shared_ptr<archx::Instruction> archx_instruction;

    UopSet uop_set;
    size_t uop_index{0};
};

} // namespace ampere::mesh

#endif // AMPERE_MESH_INJECTOR_UOP_SOURCE_IS_H
