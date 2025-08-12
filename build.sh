#!/bin/bash
# Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
# libkperf licensed under the Mulan PSL v2.
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
INSTALL_PATH=${PROJECT_DIR}/output/
BPF_DIR=${PROJECT_DIR}/pmu/bpf
BUILD_TYPE=Release
# Python module are not compiled by default.
PYTHON=false
# Test cases are not compiled by default.
INCLUDE_TEST=false
# Go support, copy so and head files
GO=false
# Bpf mode for counting
BPF=false

source ${PROJECT_DIR}/build/common.sh

creat_dir "${BUILD_DIR}"
# Specifies the gcc used by all dependencies.
export CC=gcc
export CXX=g++
PYTHON_EXE=""
PYTHON_WHL=false
if [ -d "${THIRD_PARTY}/local" ];then
  echo ${THIRD_PARTY}/local "is exist"
else
  echo ${THIRD_PARTY}local "is not exist"
  creat_dir ${THIRD_PARTY}/local
fi

for arg in "$@"; do
    case "$arg" in
        test=*)
            INCLUDE_TEST="${arg#*=}"
            ;;
        python=*)
            PYTHON="${arg#*=}"
            ;;
        install_path=*)
            INSTALL_PATH="${arg#*=}"
            ;;
        build_type=*)
            BUILD_TYPE="${arg#*=}"
            ;;
        whl=*)
            WHL="${arg#*=}"
            ;;
        python_exe=*)
            PYTHON_EXE="${arg#*=}"
            ;;
        go=*)
            GO="${arg#*=}"
            ;;
        bpf=*)
            BPF="${arg#*=}"
            ;;
    esac
done

if [[ "$INCLUDE_TEST" == "true" ]]; then
    build_googletest $THIRD_PARTY
fi

if [[ "$BPF" == "true" ]]; then
    build_libbpf $THIRD_PARTY
    build_skel_files $BPF_DIR $THIRD_PARTY
fi

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
  make --silent -j ${cpu_core_num}
  cp ./lib64/libdwarf++.a ./lib64/libelf++.a ${cmake_target_dir}
  echo "install log path: $cmake_target_dir"
}

build_libkperf()
{
    cd $BUILD_DIR
    # Remove the PYTHON_KPERF && PYTHON_WHL warning
    CMAKE_ARGS=()
    CMAKE_ARGS+=(
        "-DINCLUDE_TEST=${INCLUDE_TEST}"
        "-DPYTHON=${PYTHON}"
        "-DGO=${GO}"
        "-DCMAKE_INSTALL_PREFIX=${INSTALL_PATH}"
        "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
        "-DBPF=${BPF}"
    )
    if [ ! -z ${PYTHON_EXE} ];then
         CMAKE_ARGS+=("-DPYTHON_KPERF=${PYTHON_EXE}")
    fi
    if [ "${PYTHON}" = "true" ];then
       CMAKE_ARGS+=("-DPYTHON_WHL=${WHL}")
    fi
    cmake "${CMAKE_ARGS[@]}" ..
    make -j ${cpu_core_num}
    make install
    echo "build libkperf success"
}

function build_test()
{
    if [ "$INCLUDE_TEST" = "true" ]; then
        execute_binary "$PROJECT_DIR"
    fi
}

main() {
    build_elfin
    build_libkperf
    build_test
}

# bash build.sh test=true installPath=/home/ build_type=Release .The last three settings are optional.
main $@

