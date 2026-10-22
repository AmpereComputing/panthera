# Copyright (c) 2020-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

include (cmake/pha_fsm.cmake)

tab2enum(TAB tracker.tab HFILE tracker.enum.h CCFILE tracker.enum.cc PYTHON tracker.py)
tab2enum(TAB chi/opcodes.tab HFILE chi/opcodes.enum.h CCFILE chi/opcodes.enum.cc PYTHON chi_opcodes.py)

set(nfiles 32)
build_pha_fsm(XLSX pha_fsm.xlsx NFILES ${nfiles})

build_pha_state_fsm(XLSX CacheStateExpandedTable.xlsx)

add_custom_target(generate-headers DEPENDS tracker.enum.h chi/opcodes.enum.h)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/tracker.py" "${CMAKE_CURRENT_BINARY_DIR}/chi_opcodes.py" DESTINATION lib/python/panthera/enums)

add_library(chi_headers INTERFACE)
add_dependencies(chi_headers generate-headers)

math(EXPR limit "${nfiles} - 1")
foreach(indx RANGE ${limit})
   list(APPEND genfiles "${CMAKE_CURRENT_BINARY_DIR}/pha_fsm_${indx}.cc")
endforeach()

add_library(ampere_mesh_raw
            adi.cc
            adi.h
            chi/snoop.h
            chi/snoop.cc
            chi/types.h
            chi/types.cc
            chi/opcodes.enum.h
            chi/opcodes.enum.cc
            cbusy_base.h
            cbusy_base.cc
            cbusy.h
            cbusy.cc
            rcbusy.cc
            rcbusy.h
            matrixcbusy.cc
            matrixcbusy.h
            control_module.h
            control_module.cc
            cpx.cc
            cpx.h
            home_node_tracker.cc
            home_node_tracker.h
            home_node_tracker_entry.h
            home_node_tracker_fsm.h
            home_node_tracker_fsm.cc
            pha.h
            pha.cc
            pha_retry.h
            pha_retry.cc
            pha_phase.h
            pha_phase.cc
            ${CMAKE_CURRENT_BINARY_DIR}/pha_fsm.h
            ${CMAKE_CURRENT_BINARY_DIR}/pha_fsm_funcs.h
            ${CMAKE_CURRENT_BINARY_DIR}/pha_fsm.cc
            ${genfiles}
            ${CMAKE_CURRENT_BINARY_DIR}/pha_state_fsm.cc
            pha_state_fsm.h
            pha_tracker.cc
            pha_tracker.h
            pha_tracker_entry.h
            prefetchtgt_gen.cc
            prefetchtgt_gen.h
            injector/mesh_injector.cc
            injector/mesh_injector.h
            injector/ior_injector.cc
            injector/ior_injector.h
            injector/io_tracker.h
            injector/uop_source.h
            injector/uop_source_is.cc
            injector/uop_source_is.h
            injector/uop_source_prog.h
            injector/uop_source_prog.cc
            injector/chi_injector.cc
            injector/chi_injector.h
            responder/chi_responder.cc
            responder/chi_responder.h
            mesh.cc
            mesh.h
            mesh_config.h
            mesh_dat.h
            mesh_device.cc
            mesh_device.h
            mesh_module.cc
            mesh_module.h
            mesh_node.cc
            mesh_node.h
            mesh_port.h
            mesh_port.cc
            mesh_req.h
            mesh_req.cc
            mesh_rsp.h
            mesh_snp.h
            pager.cc
            pager.h
            idt.cc
            idt.h
            rdi.cc
            rdi.h
            rdi_packing.cc
            rdi_packing.h
            sam.cc
            sam.h
            slc.cc
            slc.h
            home_node.cc
            home_node.h
            retry.cc
            retry.h
            snoop_filter.cc
            snoop_filter.h
            tracker.enum.h
            tracker.enum.cc)

if(Boost_SERIALIZATION_FOUND)
target_link_libraries(ampere_mesh_raw PUBLIC ampere_sc archx uarch_common memory_stream Boost::serialization)
else()
target_link_libraries(ampere_mesh_raw PUBLIC ampere_sc archx uarch_common memory_stream)
endif()
add_library(ampere_mesh INTERFACE)
target_link_libraries(ampere_mesh INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,ampere_mesh_raw>")

add_executable(ampereinjmem injector/inj_mem.cc)
target_link_libraries(ampereinjmem  ampere_mesh ampere_memory ampere_core archx)
install(TARGETS ampereinjmem DESTINATION bin)

add_subdirectory(test)
