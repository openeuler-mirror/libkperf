include_directories(${CMAKE_CURRENT_LIST_DIR}/../../symbol)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../util)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../pmu)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../pmu/pfm)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../pmu/analyzer/metric)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../pmu/decoder)
include_directories(${PROJECT_TOP_DIR}/include)
add_compile_options(-g)
set(CMAKE_CXX_STANDARD 14)
aux_source_directory(. SOURCE_SRC)
add_executable(test_perf ${SOURCE_SRC} ${CMAKE_CURRENT_LIST_DIR}/../../util/pcerr.cpp)
target_link_libraries(test_perf sym kperf gtest m gtest_main elf_static dwarf_static pthread -g)

add_subdirectory(case)
