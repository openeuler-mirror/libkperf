cmake_minimum_required(VERSION 3.12.0)
set(CMAKE_CXX_STANDARD 14)

project(libkperf_tools)

set(TOOLS_BIN_DIR "${CMAKE_CURRENT_LIST_DIR}/bin")
file(MAKE_DIRECTORY "${TOOLS_BIN_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${TOOLS_BIN_DIR}")

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_library(tools_common_deps INTERFACE)
target_include_directories(tools_common_deps INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/../output/include
)
target_link_libraries(tools_common_deps INTERFACE Threads::Threads)
add_library(tools_perf_deps INTERFACE)

set(LIB_DIR "${CMAKE_CURRENT_LIST_DIR}/../output/lib")
find_library(SYM_LIB   NAMES sym   PATHS "${LIB_DIR}" NO_DEFAULT_PATH)
find_library(KPERF_LIB NAMES kperf PATHS "${LIB_DIR}" NO_DEFAULT_PATH)

target_link_libraries(tools_perf_deps INTERFACE
  tools_common_deps
  "${SYM_LIB}"
  "${KPERF_LIB}"
)

set(LIBKPERF_BUILD_PMU_DATASRC_BIN ON CACHE BOOL "" FORCE)
add_subdirectory(cache_collect)
add_subdirectory(case)
add_subdirectory(false_sharing)

add_executable(pmu_hotspot "${CMAKE_CURRENT_LIST_DIR}/pmu_hotspot/pmu_hotspot.cpp")
target_link_libraries(pmu_hotspot PRIVATE tools_perf_deps)
add_executable(pmu_perfdata "${CMAKE_CURRENT_LIST_DIR}/pmu_perfdata.cpp")
target_link_libraries(pmu_perfdata PRIVATE tools_perf_deps)
