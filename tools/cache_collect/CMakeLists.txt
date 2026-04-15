include_directories(${CMAKE_CURRENT_LIST_DIR}/../../util)
file(GLOB SRC_FILES ${CMAKE_CURRENT_LIST_DIR}/*.cpp)
add_executable(cache_collect ${SRC_FILES})

target_link_libraries(cache_collect PRIVATE tools_perf_deps)