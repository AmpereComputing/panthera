//
// Copyright(c) 2022 Ampere Computing. All Rights Reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include "ampere/evp/plugin.h"

#include "ampere/protobuf_utils/protobuf_stream.h"

namespace ampere::evp {

class CaptureBase : public Plugin
{
public:
    CaptureBase(ProcessorIfc& proc_ifc, Args& args);

    void define_event(const Definition& event_def) override;
    void define_value(const Definition& value_def) override;
    void define_enumeration(const Enumeration& enum_def) override;
    void report_parameter(const Parameter& parameter) override;
    void process_event(const Event& event) override;
    void start_simulation() override;
    void end_simulation() override;
    void report(MetricTableTimeSeries& ts) override;

    static const char* category() { return "(ES output)"; }

protected:
    std::unique_ptr<ProtobufStreamWriter> output_stream;

    bool logging{true};

    int num_event_defs{0};
    int num_value_defs{0};
    int num_enum_defs{0};
    int num_parameters{0};
    int num_events{0};
};

} // namespace ampere::evp
