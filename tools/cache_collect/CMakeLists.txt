cmake_minimum_required (VERSION 3.12.0)
project(cache_collect)

include_directories(${CMAKE_CURRENT_LIST_DIR}/../../output/include)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../util)
file(GLOB SRC_FILES ${CMAKE_CURRENT_LIST_DIR}/*.cpp)

add_executable(cache_collect ${SRC_FILES})

target_link_directories(cache_collect PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../output/lib)
target_link_libraries(cache_collect PRIVATE sym kperf)