#!/bin/bash
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
# Description: Building mainstream processes.

set -e
set -x
CURRENT_DIR=$(cd $(dirname "$0"); pwd)
PROJECT_DIR=$(realpath "${CURRENT_DIR}")

BUILD_DIR=${PROJECT_DIR}/_build
THIRD_PARTY=${PROJECT_DIR}/third_party/
source ${PROJECT_DIR}/build/common.sh

creat_dir "${BUILD_DIR}"
# 指定所有依赖使用的gcc
export CC=gcc
export CXX=g++
if [ -d "${THIRD_PARTY}/local" ];then
  echo ${THIRD_PARTY}/local "is exist"
else
  echo ${THIRD_PARTY}local "is not exist"
  creat_dir ${THIRD_PARTY}/local
fi

# build libprof.so libraries including libprocfs.so libprocfs.a libpmu.so libpmu.a libtrace.so libtrace.so
function build_elfin() {
  local cmake_target_dir=$THIRD_PARTY/local/elfin-parser
  rm -rf ${cmake_target_dir}
  if [ -d "${cmake_target_dir}" ];then
    echo ${cmake_target_dir} "is exist"
    return
  else
    echo ${cmake_target_dir} "is not exist"
    mkdir ${cmake_target_dir}
  fi
  cd "$THIRD_PARTY/elfin-parser"
  rm -rf build
  sed -i 's/-mcpu=tsv110//g' Common.cmake
  sed -i 's/-mno-outline-atomics//g' Common.cmake
  sed -i 's/-march=armv8.2-a//g' Common.cmake
  if ! grep -q "^add_compile_options(-Wno-error=switch-enum)" CMakeLists.txt; then
     sed -i '1i\add_compile_options(-Wno-error=switch-enum)' CMakeLists.txt
  fi
  mkdir build
  cd build
  cmake -DCMAKE_INSTALL_PREFIX=${cmake_target_dir} -DCMAKE_CXX_FLAGS="-fPIC" ..
  make --silent -j 64
  cp ./lib64/libdwarf++.a ./lib64/libelf++.a ${cmake_target_dir}
  echo "install log path: $cmake_target_dir"
}

build_libprof()
{
    cd $BUILD_DIR
    cmake ..
    make -j ${cpu_core_num}
    make install
    echo "build_libprof success"
}

main() {
    build_elfin
    build_libprof
}

main $@

