# Description: Compile Devikit rpc framework.
# Copyright: Copyright © Huawei Technologies Co., Ltd. 2023. All rights reserved.
# History: 2023-05-10 created

if (POLICY CMP0048)
    cmake_policy(SET CMP0048 NEW)
endif (POLICY CMP0048)
project(libkprof)
cmake_minimum_required (VERSION 3.12.0)
# Related directory settings                        #
set(UTIL_FILE_DIR ${PROJECT_TOP_DIR}/util)
set(SYMBOL_FILE_DIR ${PROJECT_TOP_DIR}/symbol)
set(SKELETON_FILE_DIR ${PROJECT_TOP_DIR}/skeleton)
set(PMU_FILE_DIR ${PROJECT_TOP_DIR}/pmu)
set(PFM_FILE_DIR ${PROJECT_TOP_DIR}/pmu/pfm)
set(PMU_DECODER_DIR ${PMU_FILE_DIR}/decoder)

# Source files                                      #
file(GLOB UTIL_SRC ${UTIL_FILE_DIR}/*.cpp)

file(GLOB PMU_SRC ${PMU_FILE_DIR}/*c ${PMU_FILE_DIR}/*cpp ${PMU_FILE_DIR}/analyzer/*cpp
        ${PMU_FILE_DIR}/analyzer/metric/*cpp ${PMU_FILE_DIR}/analyzer/numafast/*c
        ${PMU_FILE_DIR}/analyzer/numafast/util/*c ${PMU_FILE_DIR}/analyzer/numafast/analyze/*c)
file(GLOB PMU_DECODER_SRC ${PMU_DECODER_DIR}/*.cpp)
file(GLOB SYMBOL_SRC ${SYMBOL_FILE_DIR}/*c ${SYMBOL_FILE_DIR}/*cpp)
file(GLOB PFM_SRC ${PFM_FILE_DIR}/*c ${PFM_FILE_DIR}/*cpp)

set(PFM_FILE_DIR ${PROJECT_TOP_DIR}/pmu/pfm)
file(GLOB PFM_SRC ${PFM_FILE_DIR}/*c ${PFM_FILE_DIR}/*cpp)

include_directories(${PROJECT_TOP_DIR}/include)
include_directories(${PMU_FILE_DIR}/)
include_directories(${PROJECT_TOP_DIR}/include)
include_directories(${PFM_FILE_DIR})
include_directories(${PROJECT_TOP_DIR}/include)

# directories for ultilities and symbol resolving
include_directories(${UTIL_FILE_DIR})
include_directories(${SYMBOL_FILE_DIR})
include_directories(${PMU_FILE_DIR}/analyzer)
include_directories(${PMU_FILE_DIR}/analyzer/metric)
include_directories(${PMU_FILE_DIR}/analyzer/numafast)
include_directories(${PMU_FILE_DIR}/analyzer/numafast/util)
include_directories(${PMU_FILE_DIR}/analyzer/numafast/analyze)
include_directories(${PMU_DECODER_DIR})
# include skeleton files
include_directories(${SKELETON_FILE_DIR})

# include all the procfs related header files
include_directories(${PROCFS_FILE_DIR})
include_directories(${PROCFS_FILE_DIR}/analyze)
include_directories(${PROCFS_FILE_DIR}/parser)

#include thirdparty
include_directories(${THIRD_PARTY}/json/include)

set(CMAKE_SKIP_RPATH YES)

ADD_LIBRARY(perf SHARED ${PMU_SRC} ${UTIL_SRC} ${PFM_SRC} ${PMU_DECODER_SRC})
target_link_libraries(perf numa sym)
target_compile_options(perf PRIVATE -fPIC)
