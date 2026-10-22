# Copyright (c) 2023-2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

tab2enum(TAB l2c_opcode.tab HFILE l2c_opcode.enum.h CCFILE l2c_opcode.enum.cc PYTHON l2c_opcode.py)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/l2c_opcode.py" DESTINATION lib/python/panthera/enums)
add_dependencies(ampere_core_raw l2c_opcode_enum)

target_sources(ampere_core_raw PRIVATE
    l2c.h
    l2c.cc
    l2c_arb.cc
    l2c_bank.h
    l2c_bank.cc
    l2c_fb.h
    l2c_fb.cc
    l2c_line.h
    l2c_op.h
    l2c_op.cc
    l2c_pipe.cc
    l2c_pwc.h
    l2c_pwc.cc
    l2c_reqqueue.h
    l2c_reqqueue.cc
    l2c_reqqueue_prefetch.h
    l2c_reqqueue_prefetch.cc
    "${CMAKE_CURRENT_BINARY_DIR}/l2c_opcode.enum.h"
    "${CMAKE_CURRENT_BINARY_DIR}/l2c_opcode.enum.cc"
)

add_subdirectory(crp)
