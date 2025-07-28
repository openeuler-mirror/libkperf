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
set(PMU_FILE_DIR ${PROJECT_TOP_DIR}/pmu)
set(PFM_FILE_DIR ${PROJECT_TOP_DIR}/pmu/pfm)
set(PMU_DECODER_DIR ${PMU_FILE_DIR}/decoder)

# Source files                                      #
file(GLOB UTIL_SRC ${UTIL_FILE_DIR}/*.cpp)
file(GLOB PMU_SRC ${PMU_FILE_DIR}/*c ${PMU_FILE_DIR}/*cpp)
file(GLOB PMU_DECODER_SRC ${PMU_DECODER_DIR}/*.cpp)
file(GLOB SYMBOL_SRC ${SYMBOL_FILE_DIR}/*c ${SYMBOL_FILE_DIR}/*cpp)
file(GLOB PFM_SRC ${PFM_FILE_DIR}/*c ${PFM_FILE_DIR}/*cpp)

include_directories(${PROJECT_TOP_DIR}/include)
include_directories(${PMU_FILE_DIR}/)
include_directories(${PFM_FILE_DIR})

# directories for utilities and symbol resolving
include_directories(${UTIL_FILE_DIR})
include_directories(${SYMBOL_FILE_DIR})
include_directories(${PMU_DECODER_DIR})

ADD_LIBRARY(kperf SHARED ${PMU_SRC} ${UTIL_SRC} ${PFM_SRC} ${PMU_DECODER_SRC})
ADD_LIBRARY(kperf_static STATIC ${PMU_SRC} ${UTIL_SRC} ${PFM_SRC} ${PMU_DECODER_SRC})
set_target_properties(kperf_static PROPERTIES OUTPUT_NAME "kperf")
target_link_libraries(kperf sym)
target_compile_options(kperf PRIVATE -fPIC)
install(TARGETS kperf  DESTINATION ${CMAKE_INSTALL_PREFIX}/lib)
install(TARGETS kperf_static  DESTINATION ${CMAKE_INSTALL_PREFIX}/lib)
file(GLOB HEADER_FILES ${PROJECT_TOP_DIR}/include/*.h)
install(FILES ${HEADER_FILES} DESTINATION ${CMAKE_INSTALL_PREFIX}/include)
