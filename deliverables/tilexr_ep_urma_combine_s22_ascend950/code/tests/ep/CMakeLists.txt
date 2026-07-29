#
# Copyright (c) 2024-2026 TileXR Project
# CMakeLists.txt for TileXR EP tests
#

cmake_minimum_required(VERSION 3.16)
project(TileXR_EP_Tests)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)
enable_testing()
find_package(Python3 COMPONENTS Interpreter REQUIRED)

option(BUILD_TILEXR_EP_DEMO "Build TileXR EP dispatch hardware demo" OFF)
set(TILEXR_EP_URMA_COMBINE_SEND_CORE_COUNT 10 CACHE STRING
    "Number of AIV cores dedicated to EP URMA combine sending")
set(TILEXR_EP_URMA_DOORBELL_BATCH_SIZE 1 CACHE STRING
    "EP URMA combine WQEs submitted per SQ doorbell")
set(TILEXR_EP_URMA_TX_READY_BATCH_SIZE 1 CACHE STRING
    "EP URMA combine completed TX routes published per visibility barrier")
set(TILEXR_EP_URMA_TX_READY_SHARED_FLAG 0 CACHE STRING
    "EP URMA combine TX-ready publication: 0=one flag per route, 1=one shared flag per batch")
set(TILEXR_EP_URMA_TX_READY_IN_DATA 0 CACHE STRING
    "EP URMA combine TX-ready publication: 0=separate control line, 1=quant-header word")
set(TILEXR_EP_URMA_TX_META_PREFETCH_FULL 0 CACHE STRING
    "EP URMA combine Send metadata access: 0=per-route GM loads, 1=full-table MTE prefetch")
set(TILEXR_EP_URMA_RX_SCHEDULER 0 CACHE STRING
    "EP URMA combine Receive scheduler: 0=sequential, 1=round-robin, 2=round-robin+sticky")
set(TILEXR_EP_URMA_PARALLEL_ROUND_PUBLISH 0 CACHE STRING
    "EP URMA combine round publish: 0=sender0 serial, 1=all Send cores shard peers")
set(TILEXR_EP_URMA_DEFERRED_ROUND_CREDIT 0 CACHE STRING
    "EP URMA combine buffer credit: 0=wait at round tail, 1=wait before same-parity reuse")
set(TILEXR_EP_URMA_START_GATE 0 CACHE STRING
    "EP URMA combine device start gate: 0=disabled, 1=parallel Send-core rendezvous")
set(TILEXR_EP_URMA_QDC_VERSION 0 CACHE STRING
    "EP URMA combine Quant/Dequant implementation: 0=stable, 1=fast-quant/late-clear, 2=fast-quant/stable-clear, 3=half-scale/stable-clear")
set(TILEXR_EP_URMA_TX_READY_EARLY_PUBLISH 0 CACHE STRING
    "EP URMA combine Pack pipeline early ready publication")
set(TILEXR_EP_URMA_RX_READY_STICKY_MASK 0 CACHE STRING
    "EP URMA combine Receive sticky ready mask")
set(TILEXR_EP_URMA_RX_READY_BATCH_MTE2 0 CACHE STRING
    "EP URMA combine Receive batched MTE2 ready reads")
set(TILEXR_EP_URMA_RX_READY_BATCH_VECTOR 0 CACHE STRING
    "EP URMA combine Receive Vector ready reduction")
if(NOT TILEXR_EP_URMA_START_GATE STREQUAL "0" AND
        NOT TILEXR_EP_URMA_START_GATE STREQUAL "1")
    message(FATAL_ERROR "TILEXR_EP_URMA_START_GATE must be 0 or 1")
endif()
if(NOT TILEXR_EP_URMA_QDC_VERSION STREQUAL "0" AND
        NOT TILEXR_EP_URMA_QDC_VERSION STREQUAL "1" AND
        NOT TILEXR_EP_URMA_QDC_VERSION STREQUAL "2" AND
        NOT TILEXR_EP_URMA_QDC_VERSION STREQUAL "3")
    message(FATAL_ERROR "TILEXR_EP_URMA_QDC_VERSION must be 0, 1, 2, or 3")
endif()
if(NOT TILEXR_EP_URMA_TX_READY_BATCH_SIZE STREQUAL "1" AND
        NOT TILEXR_EP_URMA_TX_READY_BATCH_SIZE STREQUAL "2" AND
        NOT TILEXR_EP_URMA_TX_READY_BATCH_SIZE STREQUAL "4")
    message(FATAL_ERROR "TILEXR_EP_URMA_TX_READY_BATCH_SIZE must be 1, 2, or 4")
endif()
if(NOT TILEXR_EP_URMA_TX_READY_SHARED_FLAG STREQUAL "0" AND
        NOT TILEXR_EP_URMA_TX_READY_SHARED_FLAG STREQUAL "1")
    message(FATAL_ERROR "TILEXR_EP_URMA_TX_READY_SHARED_FLAG must be 0 or 1")
endif()
if(NOT TILEXR_EP_URMA_TX_READY_IN_DATA STREQUAL "0" AND
        NOT TILEXR_EP_URMA_TX_READY_IN_DATA STREQUAL "1")
    message(FATAL_ERROR "TILEXR_EP_URMA_TX_READY_IN_DATA must be 0 or 1")
endif()
if(NOT TILEXR_EP_URMA_TX_META_PREFETCH_FULL STREQUAL "0" AND
        NOT TILEXR_EP_URMA_TX_META_PREFETCH_FULL STREQUAL "1")
    message(FATAL_ERROR "TILEXR_EP_URMA_TX_META_PREFETCH_FULL must be 0 or 1")
endif()
foreach(tilexr_ep_bool_option
        TILEXR_EP_URMA_DEFERRED_ROUND_CREDIT
        TILEXR_EP_URMA_TX_READY_EARLY_PUBLISH
        TILEXR_EP_URMA_RX_READY_STICKY_MASK
        TILEXR_EP_URMA_RX_READY_BATCH_MTE2
        TILEXR_EP_URMA_RX_READY_BATCH_VECTOR)
    if(NOT ${tilexr_ep_bool_option} STREQUAL "0" AND
            NOT ${tilexr_ep_bool_option} STREQUAL "1")
        message(FATAL_ERROR "${tilexr_ep_bool_option} must be 0 or 1")
    endif()
endforeach()
if(TILEXR_EP_URMA_DEFERRED_ROUND_CREDIT STREQUAL "1" AND
        NOT TILEXR_EP_URMA_PARALLEL_ROUND_PUBLISH STREQUAL "1")
    message(FATAL_ERROR
        "TILEXR_EP_URMA_DEFERRED_ROUND_CREDIT requires parallel round publish")
endif()
if(TILEXR_EP_URMA_TX_READY_IN_DATA STREQUAL "1")
    if(NOT TILEXR_EP_URMA_TX_READY_BATCH_SIZE STREQUAL "1" OR
            NOT TILEXR_EP_URMA_TX_READY_SHARED_FLAG STREQUAL "0")
        message(FATAL_ERROR
            "TILEXR_EP_URMA_TX_READY_IN_DATA requires TX-ready batch 1 and shared-ready disabled")
    endif()
endif()
if(TILEXR_EP_URMA_TX_READY_EARLY_PUBLISH STREQUAL "1" AND
        (NOT TILEXR_EP_URMA_TX_READY_IN_DATA STREQUAL "0" OR
         NOT TILEXR_EP_URMA_TX_READY_BATCH_SIZE STREQUAL "1" OR
         NOT TILEXR_EP_URMA_TX_READY_SHARED_FLAG STREQUAL "0"))
    message(FATAL_ERROR
        "TILEXR_EP_URMA_TX_READY_EARLY_PUBLISH requires separate per-route TX-ready lines")
endif()
if((TILEXR_EP_URMA_RX_READY_STICKY_MASK STREQUAL "1" OR
        TILEXR_EP_URMA_RX_READY_BATCH_MTE2 STREQUAL "1") AND
        TILEXR_EP_URMA_RX_SCHEDULER STREQUAL "0")
    message(FATAL_ERROR
        "RX sticky masks and batched ready reads require a round-robin scheduler")
endif()
if(TILEXR_EP_URMA_RX_READY_BATCH_VECTOR STREQUAL "1" AND
        NOT TILEXR_EP_URMA_RX_READY_BATCH_MTE2 STREQUAL "1")
    message(FATAL_ERROR
        "TILEXR_EP_URMA_RX_READY_BATCH_VECTOR requires TILEXR_EP_URMA_RX_READY_BATCH_MTE2=1")
endif()
set(TILEXR_UDMA_QP_COUNT "${TILEXR_EP_URMA_COMBINE_SEND_CORE_COUNT}" CACHE STRING
    "Independent UDMA QPs in the TileXR build used by the demo")
if(NOT TILEXR_UDMA_QP_COUNT MATCHES "^[1-9][0-9]*$" OR
        TILEXR_UDMA_QP_COUNT LESS TILEXR_EP_URMA_COMBINE_SEND_CORE_COUNT)
    message(FATAL_ERROR "TILEXR_UDMA_QP_COUNT must cover every EP URMA combine Send core")
endif()

set(TILEXR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(TILEXR_INSTALL_PREFIX "${TILEXR_ROOT}/install" CACHE PATH "TileXR install prefix")
set(TILEXR_INSTALL_LIBDIR "${CMAKE_INSTALL_LIBDIR}" CACHE STRING "TileXR install library directory")
set(TILEXR_INSTALL_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}" CACHE STRING "TileXR install include directory")

set(_tilexr_default_ascend_home_path "$ENV{ASCEND_HOME_PATH}")
if(NOT _tilexr_default_ascend_home_path)
    set(_tilexr_default_ascend_home_path "/usr/local/Ascend/ascend-toolkit/latest")
endif()
set(ASCEND_HOME_PATH "${_tilexr_default_ascend_home_path}" CACHE PATH "Ascend toolkit path")

set(_tilexr_default_arch "$ENV{ARCH}")
if(NOT _tilexr_default_arch)
    set(_tilexr_default_arch "$ENV{TILEXR_OS_ARCH}")
endif()
if(NOT _tilexr_default_arch)
    execute_process(
        COMMAND uname -m
        OUTPUT_VARIABLE _tilexr_default_arch
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(_tilexr_default_arch STREQUAL "arm64")
        set(_tilexr_default_arch "aarch64")
    endif()
endif()
set(ARCH "${_tilexr_default_arch}" CACHE STRING "Target OS architecture")

set(_tilexr_default_ascend_driver_path "$ENV{ASCEND_DRIVER_PATH}")
if(NOT _tilexr_default_ascend_driver_path)
    set(_tilexr_default_ascend_driver_path "/usr/local/Ascend/driver")
endif()
set(ASCEND_DRIVER_PATH "${_tilexr_default_ascend_driver_path}" CACHE PATH "Ascend driver path")

set(TILEXR_EP_TEST_INCLUDE_DIRS
    ${TILEXR_ROOT}/src/include
    ${TILEXR_ROOT}/src/ep/common
    ${TILEXR_ROOT}/src/ep/host
)
foreach(dir
    "${ASCEND_DRIVER_PATH}/kernel/inc"
    "${ASCEND_HOME_PATH}/${ARCH}-linux/pkg_inc"
    "${ASCEND_HOME_PATH}/${ARCH}-linux/pkg_inc/runtime"
    "${ASCEND_HOME_PATH}/${ARCH}-linux/include"
)
    if(EXISTS "${dir}")
        list(APPEND TILEXR_EP_TEST_INCLUDE_DIRS "${dir}")
    endif()
endforeach()

find_path(TILEXR_EP_ACL_BASE_INCLUDE_DIR acl/acl_base.h
    PATHS ${TILEXR_EP_TEST_INCLUDE_DIRS}
    NO_DEFAULT_PATH
)
if(NOT TILEXR_EP_ACL_BASE_INCLUDE_DIR)
    set(TILEXR_EP_ACL_STUB_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_acl_stub")
    file(MAKE_DIRECTORY "${TILEXR_EP_ACL_STUB_DIR}/acl")
    file(WRITE "${TILEXR_EP_ACL_STUB_DIR}/acl/acl_base.h"
"#ifndef TILEXR_EP_TEST_ACL_BASE_H\n#define TILEXR_EP_TEST_ACL_BASE_H\ntypedef void *aclrtStream;\n#endif\n")
    list(APPEND TILEXR_EP_TEST_INCLUDE_DIRS "${TILEXR_EP_ACL_STUB_DIR}")
endif()

add_executable(test_tilexr_ep_layout
    unit/test_tilexr_ep_layout.cpp
    ${TILEXR_ROOT}/src/ep/host/ep_layout.cpp
    ${TILEXR_ROOT}/src/ep/host/ep_urma_combine_layout.cpp
)
target_compile_definitions(test_tilexr_ep_layout PRIVATE
    TILEXR_EP_URMA_COMBINE_SEND_CORE_COUNT=${TILEXR_EP_URMA_COMBINE_SEND_CORE_COUNT}
    TILEXR_EP_URMA_DOORBELL_BATCH_SIZE=${TILEXR_EP_URMA_DOORBELL_BATCH_SIZE}
    TILEXR_EP_URMA_TX_READY_BATCH_SIZE=${TILEXR_EP_URMA_TX_READY_BATCH_SIZE}
    TILEXR_EP_URMA_TX_READY_SHARED_FLAG=${TILEXR_EP_URMA_TX_READY_SHARED_FLAG}
    TILEXR_EP_URMA_TX_READY_IN_DATA=${TILEXR_EP_URMA_TX_READY_IN_DATA}
    TILEXR_EP_URMA_TX_META_PREFETCH_FULL=${TILEXR_EP_URMA_TX_META_PREFETCH_FULL}
    TILEXR_EP_URMA_TX_READY_EARLY_PUBLISH=${TILEXR_EP_URMA_TX_READY_EARLY_PUBLISH}
    TILEXR_EP_URMA_RX_READY_STICKY_MASK=${TILEXR_EP_URMA_RX_READY_STICKY_MASK}
    TILEXR_EP_URMA_RX_READY_BATCH_MTE2=${TILEXR_EP_URMA_RX_READY_BATCH_MTE2}
    TILEXR_EP_URMA_RX_READY_BATCH_VECTOR=${TILEXR_EP_URMA_RX_READY_BATCH_VECTOR}
    TILEXR_EP_URMA_RX_SCHEDULER=${TILEXR_EP_URMA_RX_SCHEDULER}
    TILEXR_EP_URMA_PARALLEL_ROUND_PUBLISH=${TILEXR_EP_URMA_PARALLEL_ROUND_PUBLISH}
    TILEXR_EP_URMA_DEFERRED_ROUND_CREDIT=${TILEXR_EP_URMA_DEFERRED_ROUND_CREDIT}
    TILEXR_EP_URMA_START_GATE=${TILEXR_EP_URMA_START_GATE})

add_executable(test_tilexr_ep_start_gate_layout
    unit/test_tilexr_ep_layout.cpp
    ${TILEXR_ROOT}/src/ep/host/ep_layout.cpp
    ${TILEXR_ROOT}/src/ep/host/ep_urma_combine_layout.cpp
)
target_compile_definitions(test_tilexr_ep_start_gate_layout PRIVATE
    TILEXR_EP_URMA_COMBINE_SEND_CORE_COUNT=${TILEXR_EP_URMA_COMBINE_SEND_CORE_COUNT}
    TILEXR_EP_URMA_DOORBELL_BATCH_SIZE=${TILEXR_EP_URMA_DOORBELL_BATCH_SIZE}
    TILEXR_EP_URMA_TX_READY_BATCH_SIZE=${TILEXR_EP_URMA_TX_READY_BATCH_SIZE}
    TILEXR_EP_URMA_TX_READY_SHARED_FLAG=${TILEXR_EP_URMA_TX_READY_SHARED_FLAG}
    TILEXR_EP_URMA_TX_READY_IN_DATA=${TILEXR_EP_URMA_TX_READY_IN_DATA}
    TILEXR_EP_URMA_TX_META_PREFETCH_FULL=${TILEXR_EP_URMA_TX_META_PREFETCH_FULL}
    TILEXR_EP_URMA_TX_READY_EARLY_PUBLISH=${TILEXR_EP_URMA_TX_READY_EARLY_PUBLISH}
    TILEXR_EP_URMA_RX_READY_STICKY_MASK=${TILEXR_EP_URMA_RX_READY_STICKY_MASK}
    TILEXR_EP_URMA_RX_READY_BATCH_MTE2=${TILEXR_EP_URMA_RX_READY_BATCH_MTE2}
    TILEXR_EP_URMA_RX_READY_BATCH_VECTOR=${TILEXR_EP_URMA_RX_READY_BATCH_VECTOR}
    TILEXR_EP_URMA_RX_SCHEDULER=${TILEXR_EP_URMA_RX_SCHEDULER}
    TILEXR_EP_URMA_PARALLEL_ROUND_PUBLISH=${TILEXR_EP_URMA_PARALLEL_ROUND_PUBLISH}
    TILEXR_EP_URMA_DEFERRED_ROUND_CREDIT=${TILEXR_EP_URMA_DEFERRED_ROUND_CREDIT}
    TILEXR_EP_URMA_START_GATE=1)

add_executable(test_tilexr_ep_api_sources
    unit/test_tilexr_ep_api_sources.cpp
)

add_executable(test_tilexr_ep_kernel_sources unit/test_tilexr_ep_kernel_sources.cpp)

add_executable(test_tilexr_ep_start_gate_window
    unit/test_tilexr_ep_start_gate_window.cpp
)
target_include_directories(test_tilexr_ep_start_gate_window PRIVATE
    "${TILEXR_ROOT}/src/ep/host"
)

add_executable(test_tilexr_ep_host_validation
    unit/test_tilexr_ep_host_validation.cpp
    ${TILEXR_ROOT}/src/ep/host/ep_layout.cpp
    ${TILEXR_ROOT}/src/ep/host/ep_dispatch_host.cpp
)

target_include_directories(test_tilexr_ep_layout PRIVATE
    ${TILEXR_EP_TEST_INCLUDE_DIRS}
)

target_include_directories(test_tilexr_ep_start_gate_layout PRIVATE
    ${TILEXR_EP_TEST_INCLUDE_DIRS}
)

target_include_directories(test_tilexr_ep_host_validation PRIVATE
    ${TILEXR_EP_TEST_INCLUDE_DIRS}
)

target_compile_definitions(test_tilexr_ep_api_sources PRIVATE TILEXR_SOURCE_ROOT="${TILEXR_ROOT}")
target_compile_definitions(test_tilexr_ep_kernel_sources PRIVATE TILEXR_SOURCE_ROOT="${TILEXR_ROOT}")

add_test(NAME test_tilexr_ep_layout COMMAND test_tilexr_ep_layout)
add_test(NAME test_tilexr_ep_start_gate_layout COMMAND test_tilexr_ep_start_gate_layout)
add_test(NAME test_tilexr_ep_api_sources COMMAND test_tilexr_ep_api_sources)
add_test(NAME test_tilexr_ep_kernel_sources COMMAND test_tilexr_ep_kernel_sources)
add_test(NAME test_tilexr_ep_start_gate_window COMMAND test_tilexr_ep_start_gate_window)
add_test(NAME test_tilexr_ep_host_validation COMMAND test_tilexr_ep_host_validation)
add_test(NAME test_tilexr_ep_profile_report
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_tilexr_ep_profile_report.py)
add_test(NAME test_tilexr_ep_urma_combine_send_route_balance
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_tilexr_ep_urma_combine_send_route_balance.py)
add_test(NAME test_tilexr_ep_urma_combine_p44_s20_idaf_ab_watchdog
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_tilexr_ep_urma_combine_p44_s20_idaf_ab_watchdog.py)
add_test(NAME test_tilexr_ep_urma_combine_device_latency
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_tilexr_ep_urma_combine_device_latency.py)
add_test(NAME test_tilexr_ep_urma_combine_strict_kernel_latency
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_tilexr_ep_urma_combine_strict_kernel_latency.py)

install(TARGETS
    test_tilexr_ep_layout
    test_tilexr_ep_api_sources
    test_tilexr_ep_kernel_sources
    test_tilexr_ep_start_gate_window
    test_tilexr_ep_host_validation
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(BUILD_TILEXR_EP_DEMO)
    if(IS_ABSOLUTE "${TILEXR_INSTALL_LIBDIR}")
        set(TILEXR_INSTALL_LIB_SEARCH_DIR "${TILEXR_INSTALL_LIBDIR}")
    else()
        set(TILEXR_INSTALL_LIB_SEARCH_DIR "${TILEXR_INSTALL_PREFIX}/${TILEXR_INSTALL_LIBDIR}")
    endif()
    if(IS_ABSOLUTE "${TILEXR_INSTALL_INCLUDEDIR}")
        set(TILEXR_INSTALL_INCLUDE_SEARCH_DIR "${TILEXR_INSTALL_INCLUDEDIR}")
    else()
        set(TILEXR_INSTALL_INCLUDE_SEARCH_DIR "${TILEXR_INSTALL_PREFIX}/${TILEXR_INSTALL_INCLUDEDIR}")
    endif()

    find_library(TILEXR_EP_LIB tilexr-ep HINTS "${TILEXR_INSTALL_LIB_SEARCH_DIR}" REQUIRED)
    find_library(TILEXR_COMM_LIB tile-comm HINTS "${TILEXR_INSTALL_LIB_SEARCH_DIR}" REQUIRED)

    add_executable(tilexr_ep_dispatch_demo demo/tilexr_ep_dispatch_demo.cpp)
    add_executable(tilexr_ep_urma_combine_demo demo/tilexr_ep_urma_combine_demo.cpp)
    target_compile_features(tilexr_ep_urma_combine_demo PRIVATE cxx_std_17)
    target_compile_definitions(tilexr_ep_urma_combine_demo PRIVATE
        TILEXR_EP_URMA_COMBINE_SEND_CORE_COUNT=${TILEXR_EP_URMA_COMBINE_SEND_CORE_COUNT}
        TILEXR_EP_URMA_DOORBELL_BATCH_SIZE=${TILEXR_EP_URMA_DOORBELL_BATCH_SIZE}
        TILEXR_EP_URMA_TX_READY_BATCH_SIZE=${TILEXR_EP_URMA_TX_READY_BATCH_SIZE}
        TILEXR_EP_URMA_TX_READY_SHARED_FLAG=${TILEXR_EP_URMA_TX_READY_SHARED_FLAG}
        TILEXR_EP_URMA_TX_READY_IN_DATA=${TILEXR_EP_URMA_TX_READY_IN_DATA}
        TILEXR_EP_URMA_TX_META_PREFETCH_FULL=${TILEXR_EP_URMA_TX_META_PREFETCH_FULL}
        TILEXR_EP_URMA_TX_READY_EARLY_PUBLISH=${TILEXR_EP_URMA_TX_READY_EARLY_PUBLISH}
        TILEXR_EP_URMA_RX_READY_STICKY_MASK=${TILEXR_EP_URMA_RX_READY_STICKY_MASK}
        TILEXR_EP_URMA_RX_READY_BATCH_MTE2=${TILEXR_EP_URMA_RX_READY_BATCH_MTE2}
        TILEXR_EP_URMA_RX_READY_BATCH_VECTOR=${TILEXR_EP_URMA_RX_READY_BATCH_VECTOR}
        TILEXR_EP_URMA_RX_SCHEDULER=${TILEXR_EP_URMA_RX_SCHEDULER}
        TILEXR_EP_URMA_PARALLEL_ROUND_PUBLISH=${TILEXR_EP_URMA_PARALLEL_ROUND_PUBLISH}
        TILEXR_EP_URMA_DEFERRED_ROUND_CREDIT=${TILEXR_EP_URMA_DEFERRED_ROUND_CREDIT}
        TILEXR_EP_URMA_START_GATE=${TILEXR_EP_URMA_START_GATE}
        TILEXR_EP_URMA_QDC_VERSION=${TILEXR_EP_URMA_QDC_VERSION}
        TILEXR_UDMA_QP_COUNT_VALUE=${TILEXR_UDMA_QP_COUNT})
    target_include_directories(tilexr_ep_urma_combine_demo PRIVATE
        "${TILEXR_ROOT}/src/ep/host")

    foreach(demo_target tilexr_ep_dispatch_demo tilexr_ep_urma_combine_demo)
        target_include_directories(${demo_target} PRIVATE
            "${TILEXR_INSTALL_INCLUDE_SEARCH_DIR}"
            "${ASCEND_HOME_PATH}/${ARCH}-linux/include"
            "${ASCEND_HOME_PATH}/${ARCH}-linux/pkg_inc"
            "${ASCEND_HOME_PATH}/${ARCH}-linux/pkg_inc/runtime"
        )

        target_link_directories(${demo_target} PRIVATE
            "${TILEXR_INSTALL_LIB_SEARCH_DIR}"
            "${ASCEND_HOME_PATH}/${ARCH}-linux/lib64"
            "${ASCEND_HOME_PATH}/${ARCH}-linux/devlib"
            "${ASCEND_DRIVER_PATH}/lib64/driver"
        )

        target_link_libraries(${demo_target} PRIVATE
            ${TILEXR_EP_LIB}
            ${TILEXR_COMM_LIB}
            ascendcl
            runtime
            ascend_hal
        )
    endforeach()

    install(TARGETS tilexr_ep_dispatch_demo tilexr_ep_urma_combine_demo
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
