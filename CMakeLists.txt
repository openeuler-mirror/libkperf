# Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
# libkperf licensed under the Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#     http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
# PURPOSE.
# See the Mulan PSL v2 for more details.
# Author: Mr.Dai
# Create: 2024-04-03
# Description: Define the overall structure and behavior of the project.

if (POLICY CMP0048)
    cmake_policy(SET CMP0048 NEW)
endif (POLICY CMP0048)
set(PROJECT_TOP_DIR ${CMAKE_CURRENT_LIST_DIR})
project(libkprof)
if(NOT DEFINED CMAKE_INSTALL_PREFIX OR CMAKE_INSTALL_PREFIX STREQUAL "")
    set(CMAKE_INSTALL_PREFIX "${PROJECT_TOP_DIR}/output/" CACHE PATH "Installation directory" FORCE)
endif()
if (NOT DEFINED CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE Release)
endif()

cmake_minimum_required (VERSION 3.12.0)
set(CMAKE_CXX_STANDARD 11)
if (INCLUDE_TEST)
    set(CMAKE_CXX_STANDARD 14)
endif()
set(CMAKE_C_STANDARD 11)
if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.8.5)
    message(FATAL_ERROR "GCC 4.8.5 or newer required")
endif()

set(TOP_DIR ${PROJECT_SOURCE_DIR})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -funroll-loops -fno-plt")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -funroll-loops -fno-plt ")

if (CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(arm|aarch64)")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv8.2-a")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}  -march=armv8.2-a")
endif()

message("TOP_DIR is ${TOP_DIR}")
include(${CMAKE_CURRENT_LIST_DIR}/Common.cmake)
add_subdirectory(symbol)
add_subdirectory(pmu)

if (PYTHON)
    add_subdirectory(python)
endif()

if (GO)
    add_subdirectory(go)
endif()

if (INCLUDE_TEST)
    add_subdirectory(test)
endif()

option(BPF "Enable BPF mode" OFF)
message(STATUS "BPF support: ${BPF}")

set(CMAKE_EXPORT_COMPILE_COMMANDS True)
