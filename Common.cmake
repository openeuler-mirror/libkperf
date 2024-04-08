# Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
# gala-gopher licensed under the Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#     http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
# PURPOSE.
# See the Mulan PSL v2 for more details.
# Author: Mr.Dai
# Create: 2024-04-03
# Description: Set general configuration information.



set(CMAKE_CXX_FLAGS_DEBUG " -O0 -g -Wall -pg -fno-gnu-unique -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE " -O3 -DNDEBUG -fstack-protector-all -Wl,-z,relro,-z,now -Wall -fPIE -s -fno-gnu-unique")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O3 -g")
set(CMAKE_CXX_FLAGS_MINSIZEREL "-Os -DNDEBUG")
set(CMAKE_C_FLAGS_DEBUG " -O0 -g -Wall -pg -fno-gnu-unique -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE " -O3 -DNDEBUG -fstack-protector-all -Wl,-z,relro,-z,now -Wall -fPIE -s -fno-gnu-unique")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O3 -g")
set(CMAKE_C_FLAGS_MINSIZEREL "-Os -DNDEBUG")

set(CMAKE_EXE_LINKER_FLAGS_RELEASE " -Wl,-z,relro,-z,now,-z,noexecstack -pie -s")
set(THIRD_PARTY ${PROJECT_TOP_DIR}/../opensource)
add_compile_options(-w) # 调试阶段先去除告警

if(NOT DEFINED OPEN_SOURCE_DIR)
    set(OPEN_SOURCE_DIR "${PROJECT_SOURCE_DIR}/../opensource")
endif()

# 添加一个library
#GIT_REPOSITORY ssh://git@codehub-dg-y.huawei.com:2222/hwsecurec_group/huawei_secure_c.git
#GIT_TAG        tag_Huawei_Secure_C_V100R001C01SPC012B002_00001
add_library(securec STATIC IMPORTED)
set_property(TARGET securec PROPERTY IMPORTED_LOCATION ${OPEN_SOURCE_DIR}/local/huawei_secure_c/lib/libsecurec.a)
include_directories(${THIRD_PARTY}/huawei_secure_c/include)

# GIT_REPOSITORY ssh://git@szv-open.codehub.huawei.com:2222/OpenSourceCenter/www.sqlite.org/sqlite.git
# GIT_TAG        3.40.1
add_library(sqlite3_share STATIC IMPORTED)
set_property(TARGET sqlite3_share PROPERTY IMPORTED_LOCATION ${OPEN_SOURCE_DIR}/local/sqlite3/lib/libsqlite3.so)
include_directories(${OPEN_SOURCE_DIR}/local/sqlite3/include)

add_library(elf_static STATIC IMPORTED)
set_property(TARGET elf_static PROPERTY IMPORTED_LOCATION ${OPEN_SOURCE_DIR}/local/elfin-parser/libelf++.a)

add_library(dwarf_static STATIC IMPORTED
        include/pcerrc.h
        util/pcerr.h
        symbol/name_resolve.cpp
        symbol/name_resolve.h
        symbol/symbol.cpp
        symbol/symbol.h
        symbol/symbol_resolve.cpp)
set_property(TARGET dwarf_static PROPERTY IMPORTED_LOCATION ${OPEN_SOURCE_DIR}/local/elfin-parser/libdwarf++.a)
include_directories(${THIRD_PARTY}/elfin-parser/dwarf)
include_directories(${THIRD_PARTY}/elfin-parser/elf)

#GIT_REPOSITORY ssh://git@szv-open.codehub.huawei.com:2222/OpenSourceCenter/nlohmann/json.git
set(nlohmann_json_SOURCE_DIR  ${OPEN_SOURCE_DIR}/json)
message("nlohmann_json_SOURCE_DIR:${nlohmann_json_SOURCE_DIR}")

#GIT_REPOSITORY ssh://git@szv-open.codehub.huawei.com:2222/OpenSourceCenter/google/googletest.git
#GIT_TAG        release-1.12.1 #
include_directories("${OPEN_SOURCE_DIR}/local/googletest/include")
add_library(gtest_main STATIC IMPORTED)
set_property(TARGET gtest_main PROPERTY IMPORTED_LOCATION ${OPEN_SOURCE_DIR}/local/googletest/lib64/libgtest_main.a)
add_library(gtest STATIC IMPORTED)
set_property(TARGET gtest PROPERTY IMPORTED_LOCATION ${OPEN_SOURCE_DIR}/local/googletest/lib64/libgtest.a)
add_library(gmock_main STATIC IMPORTED)
set_property(TARGET gmock_main PROPERTY IMPORTED_LOCATION ${OPEN_SOURCE_DIR}/local/googletest/lib64/libgmock_main.a)
add_library(gmock STATIC IMPORTED)
set_property(TARGET gmock PROPERTY IMPORTED_LOCATION ${OPEN_SOURCE_DIR}/local/googletest/lib64/libgmock.a)