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
set(THIRD_PARTY ${PROJECT_TOP_DIR}/third_party)
add_compile_options(-w) # 调试阶段先去除告警

add_library(elf_static STATIC IMPORTED)
set_property(TARGET elf_static PROPERTY IMPORTED_LOCATION ${THIRD_PARTY}/local/elfin-parser/libelf++.a)

add_library(dwarf_static STATIC IMPORTED)
set_property(TARGET dwarf_static PROPERTY IMPORTED_LOCATION ${THIRD_PARTY}/local/elfin-parser/libdwarf++.a)
include_directories(${THIRD_PARTY}/elfin-parser/dwarf)
include_directories(${THIRD_PARTY}/elfin-parser/elf)
