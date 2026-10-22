//
// Copyright (c) 2018-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/kestrel/aice-sim/aice_sim.h"

using namespace ampere;

namespace ampere::kestrel::aice {

int dbg = 0; // global

Top::Top(const sc_core::sc_module_name& name, sc::Clock* clock, sc::Environment* env) : sc::Module(name, clock, env)
{
    connect(fetch.out, decode.in).name("fetch_to_decode");
    connect(decode.out, execute.in).name("decode_to_execute");

    connect(execute.load_queue_has_space_out, decode.load_queue_has_space_in)
        .name("execute_to_load_queue_has_space")
        .bandwidth(64)
        .capacity(64); // infinity
    connect(execute.store_queue_has_space_out, decode.store_queue_has_space_in)
        .name("execute_to_store_queue_has_space")
        .bandwidth(64)
        .capacity(64); // infinity

    connect(execute.load_queue_empty_out, decode.load_queue_empty_in)
        .name("execute_to_load_queue_empty")
        .bandwidth(64)
        .capacity(64); // infinity
    connect(execute.store_queue_empty_out, decode.store_queue_empty_in)
        .name("execute_to_store_queue_empty")
        .bandwidth(64)
        .capacity(64); // infinity

    connect(execute.tmem_fsms_active_out, decode.tmem_fsms_active_in)
        .name("execute_to_tmem_fsms_active")
        .bandwidth(64)
        .capacity(64); // infinity

    connect(execute.out, decode.writeback_in).name("execute_to_writeback").bandwidth(64).capacity(64); // infinity
    connect(execute.mem_req_port, memory.req_port).name("execute_to_memory").bandwidth(100).capacity(100);
    connect(execute.amr_read_done_out, decode.amr_read_done_in).name("execute_to_decode_amr_read_done");
    connect(execute.imr_write_done_out, decode.imr_write_done_in).name("execute_to_decode_imr_write_done");
    connect(execute.mar_write_done_out, decode.mar_write_done_in).name("execute_to_decode_mar_write_done");
    connect(execute.imr_scale_write_done_out, decode.imr_scale_write_done_in)
        .name("execute_to_decode_imr_scale_write_done");
    connect(execute.imr_read_done_out, decode.imr_read_done_in).name("execute_to_decode_imr_read_done");

    connect(memory.resp_port, execute.mem_resp_port).name("memory_to_execute").bandwidth(4).capacity(4);
    connect(decode.writeback_out, commit.in).name("writeback_to_commit").bandwidth(100).capacity(100);
    connect(decode.decoder_credit_release_out, fetch.decoder_credit_release_in)
        .name("decoder_credit_release")
        .bandwidth(2)
        .capacity(2);

    // share metadata
    decode.set_metadata(&metadata);
    execute.set_metadata(&metadata);
}

} // namespace ampere::kestrel::aice

