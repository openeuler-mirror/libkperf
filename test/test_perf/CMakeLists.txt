include_directories(${CMAKE_CURRENT_LIST_DIR}/../../symbol)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../util)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../pmu)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../pmu/pfm)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../pmu/analyzer/metric)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../pmu/decoder)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../pmu/trace)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../pmu/proc)
if (BPF)
    include_directories(${CMAKE_CURRENT_LIST_DIR}/../../pmu/bpf)
    add_compile_definitions(BPF_ENABLED)
endif()
include_directories(${PROJECT_TOP_DIR}/include)
add_compile_options(-g)
set(CMAKE_CXX_STANDARD 14)

option(UTRACE "Enable UTrace feature" OFF)

aux_source_directory(. SOURCE_SRC)

if (NOT UTRACE)
    list(REMOVE_ITEM SOURCE_SRC "test_utrace.cpp" "./test_utrace.cpp")
endif()

add_executable(test_perf ${SOURCE_SRC} ${CMAKE_CURRENT_LIST_DIR}/../../util/pcerr.cpp)

set(COMMON_LIBS sym kperf gtest m gtest_main pthread -g)
if (BPF)
    set(BPF_LIBS bpf)
endif()
target_link_libraries(test_perf ${COMMON_LIBS} ${BPF_LIBS})

add_subdirectory(case)
