#
# Copyright (c) 2024-2026 TileXR Project
# CMakeLists.txt for TileXR collectives command-line tools
#

include(GNUInstallDirs)

set(TILEXR_COLLECTIVE_PERF_ASCENDCL
    ${ASCEND_HOME_PATH}/${ARCH}-linux/lib64/libascendcl.so
)
if(NOT EXISTS "${TILEXR_COLLECTIVE_PERF_ASCENDCL}")
    message(FATAL_ERROR
        "libascendcl.so not found at ${TILEXR_COLLECTIVE_PERF_ASCENDCL}"
    )
endif()

set(TILEXR_COLLECTIVE_PERF_PROFILE_HELPER
    ${CMAKE_CURRENT_SOURCE_DIR}/tilexr_collective_profile_report.py
)
configure_file(
    ${TILEXR_COLLECTIVE_PERF_PROFILE_HELPER}
    ${CMAKE_CURRENT_BINARY_DIR}/tilexr_collective_profile_report.py
    COPYONLY
)

add_library(tilexr-collective-perf-tool-launcher STATIC
    src/local_launcher.cpp
)

target_include_directories(tilexr-collective-perf-tool-launcher
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(tilexr_collective_perf_tool
    src/tilexr_collective_perf.cpp
)

target_include_directories(tilexr_collective_perf_tool PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(tilexr_collective_perf_tool PRIVATE
    tilexr-collective-perf-tool-launcher
    tilexr-collectives
    ${TILEXR_COLLECTIVE_PERF_ASCENDCL}
)

target_link_options(tilexr_collective_perf_tool PRIVATE
    "LINKER:-rpath-link,${ASCEND_HOME_PATH}/${ARCH}-linux/lib64"
    "LINKER:-rpath-link,${ASCEND_DRIVER_PATH}/lib64/driver"
)

set_target_properties(tilexr_collective_perf_tool PROPERTIES
    BUILD_RPATH_USE_ORIGIN TRUE
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
)

install(TARGETS tilexr_collective_perf_tool
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(PROGRAMS
    ${TILEXR_COLLECTIVE_PERF_PROFILE_HELPER}
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(BUILD_TESTING OR TILEXR_BUILD_TESTS)
    add_executable(test_tilexr_collective_perf_tool_launcher
        tests/test_local_launcher.cpp
    )
    target_link_libraries(test_tilexr_collective_perf_tool_launcher PRIVATE
        tilexr-collective-perf-tool-launcher
    )
    add_test(
        NAME test_tilexr_collective_perf_tool_launcher
        COMMAND test_tilexr_collective_perf_tool_launcher
    )
endif()
