include_directories(${CMAKE_CURRENT_LIST_DIR}/../../symbol)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../util)
include_directories(${PROJECT_TOP_DIR}/include)
set(CMAKE_CXX_STANDARD 14)
aux_source_directory(. SOURCE_SRC)
add_executable(test_symbol ${SOURCE_SRC} ${CMAKE_CURRENT_LIST_DIR}/../../util/pcerr.cpp)
target_link_libraries(test_symbol sym gtest m gmock gtest_main elf_static dwarf_static pthread)

add_subdirectory(case)