# Copyright (c) 2018-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

set(sc_source_files
    addr.cc
    addr.h
    ancestor.h
    assert.cc
    assert.h
    clock.cc
    clock.h
    config_port.h
    connect.h
    connect.cc
    connection.cc
    connection.h
    counter.h
    credit.cc
    credit.h
    cycle.h
    debug.h
    debug_registry.cc
    debug_registry.h
    environment.cc
    event.cc
    event.h
    fifo/fifo.h
    fifo/cx_queue.h
    fifo/cx_queue_with_credit.h
    fifo/delay.h
    fifo/queue.h
    fifo/delay_bw.h
    fifo/delay_with_credit.h
    fifo/queue_bw.h
    fifo/queue_with_credit.h
    fifo/zdelay.h
    fifo/zdelay_with_credit.h
    inum.cc
    inum.h
    mimo/mimo.h
    mimo/broadcast.h
    module.cc
    module.h
    param.h
    param_registry.cc
    pipeline.h
    poly.cc
    poly.h
    putter.h
    reader.h
    report.cc
    report.h
    runnable.h
    sc.h
    submodule.h
    taker.h
    transaction.cc
    transaction.h)

add_library(ampere_sc_raw ${sc_source_files})
target_link_libraries(ampere_sc_raw PUBLIC SystemC::systemc event_stream fmt Boost::filesystem effolkronium_random json)
add_library(ampere_sc INTERFACE)
target_link_libraries(ampere_sc INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,ampere_sc_raw>")

add_subdirectory(test)
add_subdirectory(testing)
