if (POLICY CMP0048)
    cmake_policy(SET CMP0048 NEW)
endif (POLICY CMP0048)
project(libkprof)
cmake_minimum_required (VERSION 3.12.0)

set(SYMBOL_FILE_DIR ${PROJECT_TOP_DIR}/symbol)
set(INCLUDE_DIR ${PROJECT_TOP_DIR}/include)
set(UTIL_FILE_DIR ${PROJECT_TOP_DIR}/util)

file(GLOB SYMBOL_SRC ${SYMBOL_FILE_DIR}/*c ${SYMBOL_FILE_DIR}/*cpp ${UTIL_FILE_DIR}/pcerr.cpp ${UTIL_FILE_DIR}/common.cpp)

include_directories(${UTIL_FILE_DIR})
include_directories(${SYMBOL_FILE_DIR})
include_directories(${INCLUDE_DIR})

message(${THIRD_PARTY}/elfin-parser/elf)
ADD_LIBRARY(sym SHARED ${SYMBOL_SRC})
ADD_LIBRARY(sym_static STATIC ${SYMBOL_SRC})
set_target_properties(sym_static PROPERTIES OUTPUT_NAME "sym")
target_link_libraries(sym elf_static dwarf_static pthread)
install(TARGETS sym DESTINATION ${CMAKE_INSTALL_PREFIX}/lib)
install(TARGETS sym_static DESTINATION ${CMAKE_INSTALL_PREFIX}/lib)
install(FILES ${SYMBOL_FILE_DIR}/symbol.h DESTINATION ${CMAKE_INSTALL_PREFIX}/include)
