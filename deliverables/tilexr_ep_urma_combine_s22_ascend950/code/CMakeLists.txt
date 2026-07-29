# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)

project(tilexr LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 14)
option(TILEXR_BUILD_COLLECTIVES "Build optional TileXR collectives library" OFF)
option(TILEXR_BUILD_EP "Build TileXR EP communication library" OFF)
option(TILEXR_BUILD_TESTS "Build TileXR tests" OFF)
option(TILEXR_BUILD_CHECKER "Build TileXR no-NPU checker" OFF)
set(TILEXR_EP_URMA_COMBINE_SEND_CORE_COUNT 10 CACHE STRING
    "Number of AIV cores dedicated to EP URMA combine sending")
set(TILEXR_EP_URMA_DOORBELL_BATCH_SIZE 1 CACHE STRING
    "EP URMA combine WQEs submitted per SQ doorbell (1, 2, 4, or 8)")
set(TILEXR_EP_URMA_TX_READY_BATCH_SIZE 1 CACHE STRING
    "EP URMA combine completed TX routes published per visibility barrier (1, 2, or 4)")
set(TILEXR_EP_URMA_TX_READY_SHARED_FLAG 0 CACHE STRING
    "EP URMA combine TX-ready publication: 0=one flag per route, 1=one shared flag per batch")
set(TILEXR_EP_URMA_TX_READY_IN_DATA 0 CACHE STRING
    "EP URMA combine TX-ready publication: 0=separate control line, 1=quant-header word")
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
set(TILEXR_EP_URMA_TX_META_PREFETCH_FULL 0 CACHE STRING
    "EP URMA combine Send metadata access: 0=scalar loads, 1=full-table MTE prefetch")
set(TILEXR_EP_URMA_TX_READY_EARLY_PUBLISH 0 CACHE STRING
    "EP URMA combine Pack pipeline: 0=publish after current route, 1=publish previous route before V_S wait")
set(TILEXR_EP_URMA_RX_READY_STICKY_MASK 0 CACHE STRING
    "EP URMA combine Receive polling: 0=recheck routes, 1=retain ready routes per active token slot")
set(TILEXR_EP_URMA_RX_READY_BATCH_MTE2 0 CACHE STRING
    "EP URMA combine Receive polling: 0=one MTE2 wait per route, 1=one wait per pending route batch")
set(TILEXR_EP_URMA_RX_READY_BATCH_VECTOR 0 CACHE STRING
    "EP URMA combine Receive polling: 0=scalar flag checks, 1=Vector abs/reduce checks")
if(NOT TILEXR_EP_URMA_COMBINE_SEND_CORE_COUNT MATCHES "^[1-9][0-9]*$" OR
        TILEXR_EP_URMA_COMBINE_SEND_CORE_COUNT GREATER_EQUAL 64)
    message(FATAL_ERROR "TILEXR_EP_URMA_COMBINE_SEND_CORE_COUNT must be in [1, 63]")
endif()
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
set(TILEXR_UDMA_QP_COUNT "${TILEXR_EP_URMA_COMBINE_SEND_CORE_COUNT}" CACHE STRING
    "Independent UDMA QPs created per local EID")
if(NOT TILEXR_UDMA_QP_COUNT MATCHES "^[1-9][0-9]*$" OR
        TILEXR_UDMA_QP_COUNT LESS TILEXR_EP_URMA_COMBINE_SEND_CORE_COUNT)
    message(FATAL_ERROR "TILEXR_UDMA_QP_COUNT must cover every EP URMA combine Send core")
endif()
if(NOT TILEXR_EP_URMA_DOORBELL_BATCH_SIZE STREQUAL "1" AND
        NOT TILEXR_EP_URMA_DOORBELL_BATCH_SIZE STREQUAL "2" AND
        NOT TILEXR_EP_URMA_DOORBELL_BATCH_SIZE STREQUAL "4" AND
        NOT TILEXR_EP_URMA_DOORBELL_BATCH_SIZE STREQUAL "8")
    message(FATAL_ERROR "TILEXR_EP_URMA_DOORBELL_BATCH_SIZE must be 1, 2, 4, or 8")
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
if(TILEXR_EP_URMA_TX_READY_IN_DATA STREQUAL "1")
    if(NOT TILEXR_EP_URMA_TX_READY_BATCH_SIZE STREQUAL "1" OR
            NOT TILEXR_EP_URMA_TX_READY_SHARED_FLAG STREQUAL "0")
        message(FATAL_ERROR
            "TILEXR_EP_URMA_TX_READY_IN_DATA requires TX-ready batch 1 and shared-ready disabled")
    endif()
endif()
if(NOT TILEXR_EP_URMA_RX_SCHEDULER STREQUAL "0" AND
        NOT TILEXR_EP_URMA_RX_SCHEDULER STREQUAL "1" AND
        NOT TILEXR_EP_URMA_RX_SCHEDULER STREQUAL "2")
    message(FATAL_ERROR "TILEXR_EP_URMA_RX_SCHEDULER must be 0, 1, or 2")
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
if(NOT TILEXR_EP_URMA_PARALLEL_ROUND_PUBLISH STREQUAL "0" AND
        NOT TILEXR_EP_URMA_PARALLEL_ROUND_PUBLISH STREQUAL "1")
    message(FATAL_ERROR "TILEXR_EP_URMA_PARALLEL_ROUND_PUBLISH must be 0 or 1")
endif()
if(TILEXR_EP_URMA_DEFERRED_ROUND_CREDIT STREQUAL "1" AND
        NOT TILEXR_EP_URMA_PARALLEL_ROUND_PUBLISH STREQUAL "1")
    message(FATAL_ERROR
        "TILEXR_EP_URMA_DEFERRED_ROUND_CREDIT requires parallel round publish")
endif()
include(CTest)
if(BUILD_TESTING OR TILEXR_BUILD_TESTS)
    enable_testing()
endif()
if(BUILD_TESTING OR TILEXR_BUILD_TESTS)
    add_subdirectory(tests/data_as_flag)
endif()
include(GNUInstallDirs)

add_link_options(-Wl,-z,relro,-z,now)
add_link_options(-s)
# 获取环境变量
IF (CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "aarch64")
    SET(ARCH aarch64)
ELSEIF (CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "x86_64")
    SET(ARCH x86_64)
ENDIF ()
if (DEFINED ENV{ASCEND_HOME_PATH})
    set(ASCEND_HOME_PATH $ENV{ASCEND_HOME_PATH})
else ()
    message("ASCEND_HOME_PATH not set! using default path!")
    set(ASCEND_HOME_PATH /usr/local/Ascend/ascend-toolkit/latest)
endif ()

set(_tilexr_default_ascend_driver_path "$ENV{ASCEND_DRIVER_PATH}")
if(NOT _tilexr_default_ascend_driver_path)
    set(_tilexr_default_ascend_driver_path /usr/local/Ascend/driver)
endif()
set(ASCEND_DRIVER_PATH "${_tilexr_default_ascend_driver_path}" CACHE PATH "Ascend driver path")

message("== ASCEND_HOME_PATH: ${ASCEND_HOME_PATH}")
message("== ASCEND_DRIVER_PATH: ${ASCEND_DRIVER_PATH}")

include_directories(
        ${CMAKE_CURRENT_LIST_DIR}/src/include/
        ${ASCEND_HOME_PATH}/${ARCH}-linux/pkg_inc/
        ${ASCEND_HOME_PATH}/${ARCH}-linux/pkg_inc/profiling/
        ${ASCEND_HOME_PATH}/${ARCH}-linux/pkg_inc/runtime/
        ${ASCEND_HOME_PATH}/${ARCH}-linux/include/
        ${ASCEND_HOME_PATH}/${ARCH}-linux/include/hccl/
        ${ASCEND_HOME_PATH}/${ARCH}-linux/include/experiment
        ${ASCEND_HOME_PATH}/${ARCH}-linux/include/experiment/runtime/
        ${ASCEND_HOME_PATH}/${ARCH}-linux/include/experiment/msprof/
        ${CMAKE_SOURCE_DIR}/3rdparty/spdlog/include
        ${CMAKE_SOURCE_DIR}/3rdparty/
)
add_subdirectory(src/comm)
if(TILEXR_BUILD_EP)
    add_subdirectory(src/ep)
endif()
if(TILEXR_BUILD_COLLECTIVES)
    add_subdirectory(src/collectives)
    if(BUILD_TESTING OR TILEXR_BUILD_TESTS)
        add_subdirectory(tests/collectives)
        configure_file(
            tests/collectives/cmake/install_prefix_smoke.cmake.in
            ${CMAKE_BINARY_DIR}/tilexr_collectives_install_smoke.cmake
            @ONLY
        )
        add_test(NAME tilexr_collectives_install_smoke
            COMMAND ${CMAKE_COMMAND} -P ${CMAKE_BINARY_DIR}/tilexr_collectives_install_smoke.cmake)
    endif()
endif()
if(TILEXR_BUILD_CHECKER)
    add_subdirectory(tools/checker)
    if(BUILD_TESTING OR TILEXR_BUILD_TESTS)
        add_subdirectory(tests/checker)
    endif()
endif()
