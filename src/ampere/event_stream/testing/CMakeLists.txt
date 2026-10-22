# Copyright (c) 2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

add_executable(gen_es_test gen_es_test.cc)
target_link_libraries(gen_es_test event_stream fmt)

set(SOURCES event_stream_dummy.cc event_stream_dummy.h build_event_stream.h)

add_library(event_stream_testing ${SOURCES})
target_link_libraries(event_stream_testing PUBLIC fmt)
add_dependencies(event_stream_testing gen_es_test)
target_compile_definitions(event_stream_testing PUBLIC GEN_ES_BIN="$<TARGET_FILE:gen_es_test>")
