if (POLICY CMP0048)
    cmake_policy(SET CMP0048 NEW)
endif (POLICY CMP0048)
project(libprofutil)
cmake_minimum_required (VERSION 3.12.0)

set(UTIL_FILE_DIR ${PROJECT_TOP_DIR}/util)
set(SYMBOL_FILE_DIR ${PROJECT_TOP_DIR}/symbol)
set(INCLUDE_DIR ${PROJECT_TOP_DIR}/include)

file(GLOB UTIL_SRC ${UTIL_FILE_DIR}/*cpp)

include_directories(${SYMBOL_FILE_DIR})
include_directories(${UTIL_FILE_DIR})
include_directories(${INCLUDE_DIR})

include_directories(${THIRD_PARTY}/json/single_include/nlohmann)
include_directories(${THIRD_PARTY}/huawei_secure_c/include)

add_library(profu STATIC ${UTIL_SRC})
target_compile_options(profu PUBLIC -fPIC)
target_link_libraries(profu)
