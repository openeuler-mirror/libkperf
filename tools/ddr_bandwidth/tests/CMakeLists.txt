cmake_minimum_required(VERSION 3.12)
project(ddr_bw_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()
add_executable(test_ddr_bw test_attribution.cpp ../ddr_bw_attribution.cpp)
target_include_directories(test_ddr_bw PRIVATE ..)
add_test(NAME ddr_bw_attribution_and_schedule COMMAND test_ddr_bw)

if (UNIX)
    add_executable(test_ddr_bw_cli
        test_cli.cpp
        ../ddr_bw_cli.cpp
        ../ddr_bw_attribution.cpp)
    target_include_directories(test_ddr_bw_cli PRIVATE .. ../../../include)
    add_test(NAME ddr_bw_cli COMMAND test_ddr_bw_cli)
endif()
