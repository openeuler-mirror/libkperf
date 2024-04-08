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
if [ -d ${WORKSPACE}/code  ]; then
    PROJECT_DIR=${WORKSPACE}/code/libprof
else
    CURRENT_DIR=$(cd $(dirname "$0"); pwd)
    PROJECT_DIR=$(realpath "${CURRENT_DIR}")
fi
if [[ -d "${PROJECT_DIR}/../opensource" ]] ;then
    OPENSOURCE_DIR=$(realpath "${PROJECT_DIR}/../opensource")
else
    OPENSOURCE_DIR=${PROJECT_DIR}/opensource
fi
PACKAGE_NAME="DevKit-LibProf-24.0.RC1-Linux-Kunpeng"
PACKAGE_PATH="${PROJECT_DIR}/output/${PACKAGE_NAME}"
BUILD_DIR=${PROJECT_DIR}/_build
source ${PROJECT_DIR}/build/common.sh

creat_dir "${BUILD_DIR}"
creat_dir "${PACKAGE_PATH}"
# 指定所有依赖使用的gcc
export CC=gcc
export CXX=g++
if [ -d "${OPENSOURCE_DIR}/local" ];then
  echo ${OPENSOURCE_DIR} "is exist"
  else
  echo ${OPENSOURCE_DIR} "is not exist"
  mkdir ${OPENSOURCE_DIR}/local
fi

# 默认情况下不包含测试目录
INCLUDE_TEST=OFF

# 如果第一个参数是 "test"，则设置 INCLUDE_TEST 为 ON
if [[ "$1" == "test" ]]; then
    build_googletest $OPENSOURCE_DIR
    INCLUDE_TEST=ON
fi

# build libprof.so libraries including libprocfs.so libprocfs.a libpmu.so libpmu.a libtrace.so libtrace.so
function build_elfin() {
  local cmake_target_dir=$OPENSOURCE_DIR/local/elfin-parser
  rm -rf ${cmake_target_dir}
  if [ -d "${cmake_target_dir}" ];then
    echo ${cmake_target_dir} "is exist"
    return
  else
    echo ${cmake_target_dir} "is not exist"
    mkdir ${cmake_target_dir}
  fi
  cd "$OPENSOURCE_DIR/elfin-parser"
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
#   mkdir -p ${cmake_target_dir}
  cp ./lib64/libdwarf++.a ./lib64/libelf++.a ${cmake_target_dir}
#   popd
  echo "install log path: $cmake_target_dir"
  cd ../../../libprof
}

function build_huawei_securec(){
  if [ -d "${OPENSOURCE_DIR}/local" ];then
    echo ${OPENSOURCE_DIR} "is exist"
  else
    echo ${OPENSOURCE_DIR} "is not exist"
    mkdir ${OPENSOURCE_DIR}/local
  fi
  local cmake_target_dir=$OPENSOURCE_DIR/local/huawei_secure_c
  if [ -d "${cmake_target_dir}" ];then
    echo ${cmake_target_dir} "is exist"
    return
  else
    echo ${cmake_target_dir} "is not exist"
  fi
  pushd "$OPENSOURCE_DIR/huawei_secure_c/src"
  sed -i 's/-Wdate-time//g' Makefile
  sed -i 's/-Wduplicated-branches//g' Makefile
  sed -i 's/-Wduplicated-cond//g' Makefile
  sed -i 's/-Wimplicit-fallthrough=3//g' Makefile
  sed -i 's/-Wshift-negative-value//g' Makefile
  sed -i 's/-Wshift-overflow=2//g' Makefile
  make lib
  mkdir -p $cmake_target_dir
  cp -rf "$OPENSOURCE_DIR/huawei_secure_c/lib" $cmake_target_dir
  cp -rf  "$OPENSOURCE_DIR/huawei_secure_c/include" $cmake_target_dir
  popd
  echo "install log path: $cmake_target_dir"
}

build_libprof()
{
    cd $BUILD_DIR
    cmake -DINCLUDE_TEST=$INCLUDE_TEST ..
    make -j ${cpu_core_num}
    echo "build_libprof success"
}

build_package()
{
    cd $PROJECT_DIR/output
    cp -rf  ${PROJECT_DIR}/include ${PACKAGE_PATH}
    cp -rf  ${PROJECT_DIR}/symbol/symbol.h ${PACKAGE_PATH}/include/
    cp -rf  ${BUILD_DIR}/pmu/libperf.so ${PACKAGE_PATH}
    cp -rf  ${BUILD_DIR}/symbol/libsym.so ${PACKAGE_PATH}
    tar -czf "${PACKAGE_NAME}.tar.gz" "${PACKAGE_NAME}" --owner=root --group=root
    if [ "${IS_BUILDCHECK}" != "true" -a -d "${WORKSPACE}/output_${ENV_PIPELINE_JOB_ID}/libprof" ];then
        cp -rf ${PACKAGE_NAME}.tar.gz  ${WORKSPACE}/output_${ENV_PIPELINE_JOB_ID}/libprof
    fi
}

main() {
    cd $PROJECT_DIR/ && python "build/generate_config.py"
    build_elfin
    build_huawei_securec
    build_sqlite3 $OPENSOURCE_DIR
    build_libprof
    build_package
#    bash ${CURRENT_DIR}/test/test.sh ${PROJECT_DIR}/output/test ${PACKAGE_PATH}
}

# bash build.sh test来获取UT
main $@