option(LIBKPERF_BUILD_DDR_BW_BIN "Build ddr_bandwidth executable" ON)

add_library(ddr_bw_core STATIC
    "${CMAKE_CURRENT_LIST_DIR}/ddr_bw_cli.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ddr_bw_pmu.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ddr_bw_collection.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ddr_bw_attribution.cpp"
)

target_include_directories(ddr_bw_core PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}"
)
target_include_directories(ddr_bw_core PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../util"
)

if (TARGET tools_deps)
    target_link_libraries(ddr_bw_core PUBLIC tools_deps)
endif()

if (LIBKPERF_BUILD_DDR_BW_BIN)
    add_executable(ddr_bandwidth "${CMAKE_CURRENT_LIST_DIR}/main.cpp")
    target_link_libraries(ddr_bandwidth PRIVATE ddr_bw_core)
endif()
