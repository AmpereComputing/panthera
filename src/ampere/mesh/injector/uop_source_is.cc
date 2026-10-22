//
// Copyright (c) 2021-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/archx/furiosa_encode.h"
#include "ampere/archx/instruction_simple.h"
#include "ampere/mesh/injector/mesh_injector.h"
#include "ampere/mesh/injector/uop_source_is.h"
#include "ampere/mesh/mesh_req.h"

namespace ampere::mesh {

UopSourceIS::UopSourceIS(const sc_core::sc_module_name& name) : UopSource{name}
{
    if (!instruction_stream_name.val().empty())
        driver = makeDriver(instruction_stream_name, 0);
}

void UopSourceIS::end_of_elaboration()
{
    if (enabled) {
        AMPERE_ASSERT(!instruction_stream_name.val().empty(), "{}: no instruction stream", name());

        if (decoders.val().empty())
            AMPERE_ERROR("no decoders were specified");

        archx::Instruction::set_decoders(decoders);
    }
}

std::optional<IOop> UopSourceIS::fetch_io()
{
    return {};
}

std::optional<IOop> UopSourceIS::fetch_read_io()
{
    return {};
}

std::optional<IOop> UopSourceIS::fetch_write_io()
{
    return {};
}

std::pair<chi::IOType, uint64_t> UopSourceIS::peek_io()
{
    return std::make_pair(chi::IOType::Read, 0);
}

std::optional<Uop> UopSourceIS::fetch_uop()
{
    if (stopped || !enabled)
        return {};

    if (uop_index == uop_set.size()) {
        archx_instruction = driver->fetch(driver->next_pc());

        uop_set = archx_instruction->decode();
        uop_index = 0;

        ++instructions;

        if (archx_instruction->control()) {
            if (*archx_instruction->control() == START_MEASUREMENT) {
                get_environment()->event_stream->enable();
            }

            if (*archx_instruction->control() == STOP_MEASUREMENT) {
                stopped = true;
                last_uop_id = uop_id - 1;
            }
        }
    }

    return make<Uop>(++uop_id, std::move(uop_set[uop_index++]), archx_instruction);
}

void UopSourceIS::commit_uop(Uop uop)
{
    if (uop->is_end_inst())
        driver->commit(uop->archx_instruction);

    if (uop->id == last_uop_id) {
        AMPERE_INFO("Stop performance measurement after {} instruction @ {}", instructions, sc_core::sc_time_stamp());
        sc_core::sc_stop();
    }
}

} // namespace ampere::mesh
