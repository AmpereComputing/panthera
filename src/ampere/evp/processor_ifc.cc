//
// Copyright(c) 2022 Ampere Computing LLC
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/evp/processor_ifc.h"
#include <cstdint>

#include "ampere/event_stream/event_stream_reader.h"

namespace ampere::evp {

const char* ProcessorIfc::enumeration_value(int definition_id, int64_t value) const
{
    return enumeration_value_for_definition(get_definition(definition_id), value);
}

const char* ProcessorIfc::enumeration_value_for_definition(const Definition& definition, int64_t value) const
{
    if (has_enumeration(definition)) {
        const auto& enumeration = get_enumeration(definition.enumeration_id());
        return event_stream::EventStreamReader::enumeration_value(enumeration, value);
    }

    return nullptr;
}

} // namespace ampere::evp
