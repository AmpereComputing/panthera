//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/event_stream/event_stream_recorder.h"

namespace ampere::event_stream {

void EventStreamRecorder::record_event(const RecorderEventHandle& event)
{
    RecordedEvent e(event, this);
    if (callback_func)
        callback_func(e);
    else
        events.emplace_back(e);
}

void EventStreamRecorder::post_event(EventType event_type, std::uint64_t time)
{
    catch_up();
    if (listening(event_type))
        record_event({event_type, time});
}

EventHandle EventStreamRecorder::open_event(EventType event_type, std::uint64_t time)
{
    catch_up();
    if (listening(event_type))
        return event_handle_pool.construct(event_type, time);
    return event_handle_pool.construct();
}

void EventStreamRecorder::close_event(EventHandle event)
{
    auto* handle = static_cast<RecorderEventHandle*>(event);
    if (*handle)
        record_event(*handle);
    event_handle_pool.destroy(handle);
}

void EventStreamRecorder::add_int_data(EventHandle event, DataType event_data_type, std::int64_t value)
{
    auto* handle = static_cast<RecorderEventHandle*>(event);
    if (*handle)
        handle->data[event_data_type] = value;
}

void EventStreamRecorder::add_uint_data(EventHandle event, DataType event_data_type, std::uint64_t value)
{
    auto* handle = static_cast<RecorderEventHandle*>(event);
    if (*handle)
        handle->data[event_data_type] = value;
}

void EventStreamRecorder::add_string_data(EventHandle event, DataType event_data_type, const std::string& value)
{
    auto* handle = static_cast<RecorderEventHandle*>(event);
    if (*handle)
        handle->data[event_data_type] = value;
}
} // namespace ampere::event_stream

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
