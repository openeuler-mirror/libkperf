option(LIBKPERF_BUILD_PMU_DATASRC_BIN "Build pmu_datasrc executable" OFF)

add_library(pmu_datasrc_core STATIC
    "${CMAKE_CURRENT_LIST_DIR}/pmu_datasrc_core.cpp"
)

target_include_directories(pmu_datasrc_core PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}"
)

target_include_directories(pmu_datasrc_core PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../include"
    "${CMAKE_CURRENT_LIST_DIR}/../../symbol"
)

target_link_libraries(pmu_datasrc_core PUBLIC tools_perf_deps)

if (LIBKPERF_BUILD_PMU_DATASRC_BIN)
    add_executable(pmu_datasrc "${CMAKE_CURRENT_LIST_DIR}/pmu_datasrc_main.cpp")
    target_link_libraries(pmu_datasrc PRIVATE pmu_datasrc_core)
endif()