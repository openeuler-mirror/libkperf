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
# Description: libkperf python module
project(python_libkperf)

if(DEFINED PYTHON_KPERF AND NOT PYTHON_KPERF STREQUAL "")
    set(PYTHON_EXECUTABLE ${PYTHON_KPERF})
else()
    find_package(PythonInterp 3.6 REQUIRED)
    find_package(PythonLibs 3.6 REQUIRED)
endif()
message("PYTHON_EXECUTABLE is ${PYTHON_EXECUTABLE}")
add_subdirectory(modules)