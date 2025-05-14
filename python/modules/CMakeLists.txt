# Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
# libkperf licensed under the Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#     http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
# PURPOSE.
# See the Mulan PSL v2 for more details.
# Author: Victor Jin
# Create: 2024-05-16
# Description: libkperf python modules cmake

project(python_libkperf)

set(LIBKPERF_PATH ${CMAKE_BINARY_DIR}/pmu/libkperf.so)
set(LIBSYM_PATH ${CMAKE_BINARY_DIR}/symbol/libsym.so)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/setup.py.in
    ${CMAKE_CURRENT_SOURCE_DIR}/setup.py
)
if(DEFINED PYTHON_WHL AND PYTHON_WHL)
    set(BIN_TYPE "bdist_wheel")
else()
    set(BIN_TYPE "install")
endif()

add_custom_target(${PROJECT_NAME} ALL
    COMMAND ${PYTHON_EXECUTABLE} setup.py ${BIN_TYPE}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_dependencies(${PROJECT_NAME} kperf sym)