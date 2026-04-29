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
LLVM_LIB_DIR=${PROJECT_DIR}/llvm-symbolizer/build/lib
BUILD_TYPE=Release
# Python module are not compiled by default.
PYTHON=false
# Test cases are not compiled by default.
INCLUDE_TEST=false
# Go support, copy so and head files
GO=false
# Bpf mode for counting
BPF=false
# ASAN
ASAN=false

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
        asan=*)
            ASAN="${arg#*=}"
            ;;
        elf_llvm=*)
            ELF_LLVM="${arg#*=}"
            ;;
    esac
done

ARCH_TARGET="AArch64"
ARCH=$(uname -m)
case "$ARCH" in
    x86_64|amd64) ARCH_TARGET="X86" ;;
    aarch64|arm64) ARCH_TARGET="AArch64" ;;
esac

if [[ "$INCLUDE_TEST" == "true" ]]; then
    build_googletest $THIRD_PARTY
fi

if [[ "$BPF" == "true" ]]; then
    build_libbpf $THIRD_PARTY
    build_skel_files $BPF_DIR $THIRD_PARTY
fi

build_capstone $THIRD_PARTY

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

function build_symbolizer()
{
    local cmake_target_dir=$PROJECT_DIR/llvm-symbolizer/build
    rm -rf ${cmake_target_dir}
    cd $PROJECT_DIR/llvm-symbolizer
    mkdir build
    cd build
    cmake -DCMKAE_BUILD_TYPE=Release -DLLVM_TARGETS_TO_BUILD="${ARCH_TARGET}" ..
    make -j ${cpu_core_num}
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
        "-DARCH_TARGET=${ARCH_TARGET}"
        "-DELF_LLVM=${ELF_LLVM}"
    )
    if [ ! -z ${PYTHON_EXE} ];then
         CMAKE_ARGS+=("-DPYTHON_KPERF=${PYTHON_EXE}")
    fi
    if [ "${PYTHON}" = "true" ];then
       CMAKE_ARGS+=("-DPYTHON_WHL=${WHL}")
    fi
    if [ "${BPF}" = "true" ];then
       CMAKE_ARGS+=(
            "-DCMAKE_INSTALL_RPATH=${INSTALL_PATH}lib;${THIRD_PARTY}local/bpf/usr/lib64"
            "-DCMAKE_INSTALL_RPATH_USE_LINK_PATH=TRUE"
        )
    fi
    if [ "${ASAN}" = "true" ];then
       CMAKE_ARGS+=(
            "-DCMAKE_C_FLAGS=-fsanitize=address -fno-omit-frame-pointer"
            "-DCAMKE_CXX_FLASG=-fsanitize=address -fno-omit-frame-pointer"
            "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address"
            "-DCAMKE_SHARED_LINKPER_FLAGS=-fsanitize=address"
       )
    fi
    cmake "${CMAKE_ARGS[@]}" ..
    make -j ${cpu_core_num}
    make install
    echo "build libkperf success"
}

function merge_libsym()
{
    set +x
    cd ${INSTALL_PATH}/lib
    echo "CREATE libsym_bak.a" > merge.mri
    
    #copy avoid ++ error
    if [ ! -f "${THIRD_PARTY}/local/elfin-parser/libelf.a" ];then
        cp "${THIRD_PARTY}/local/elfin-parser/libelf++.a" "${THIRD_PARTY}/local/elfin-parser/libelf.a"
    fi
    echo "ADDLIB ${THIRD_PARTY}/local/elfin-parser/libelf.a" >> merge.mri
    echo "ADDLIB ${INSTALL_PATH}/lib/libsym.a" >> merge.mri
    echo "ADDLIB ${LLVM_LIB_DIR}/libLLVM${ARCH_TARGET}AsmPrinter.a" >> merge.mri
    echo "ADDLIB ${LLVM_LIB_DIR}/libLLVM${ARCH_TARGET}AsmParser.a" >> merge.mri
    echo "ADDLIB ${LLVM_LIB_DIR}/libLLVM${ARCH_TARGET}Desc.a" >> merge.mri
    echo "ADDLIB ${LLVM_LIB_DIR}/libLLVM${ARCH_TARGET}Disassembler.a" >> merge.mri
    echo "ADDLIB ${LLVM_LIB_DIR}/libLLVM${ARCH_TARGET}Info.a" >> merge.mri
    echo "ADDLIB ${LLVM_LIB_DIR}/libLLVM${ARCH_TARGET}Utils.a" >> merge.mri

    echo "ADDLIB ${LLVM_LIB_DIR}/libLLVMTarget.a" >> merge.mri
    echo "ADDLIB ${LLVM_LIB_DIR}/libLLVMDebugInfoDWARF.a" >> merge.mri
    echo "ADDLIB ${LLVM_LIB_DIR}/libLLVMDemangle.a" >> merge.mri
    echo "ADDLIB ${LLVM_LIB_DIR}/libLLVMObject.a" >> merge.mri
    echo "ADDLIB ${LLVM_LIB_DIR}/libLLVMOption.a" >> merge.mri
    echo "ADDLIB ${LLVM_LIB_DIR}/libLLVMSupport.a" >> merge.mri
    echo "ADDLIB ${LLVM_LIB_DIR}/libLLVMSymbolize.a" >> merge.mri
    echo "ADDLIB ${LLVM_LIB_DIR}/libLLVMDebugInfoDWARF.a" >> merge.mri
    echo "ADDLIB ${LLVM_LIB_DIR}/libLLVMObject.a" >> merge.mri
    echo "ADDLIB ${LLVM_LIB_DIR}/libLLVMBitReader.a" >> merge.mri
    echo "ADDLIB ${LLVM_LIB_DIR}/libLLVMCore.a" >> merge.mri
    echo "ADDLIB ${LLVM_LIB_DIR}/libLLVMMCParser.a" >> merge.mri
    echo "ADDLIB ${LLVM_LIB_DIR}/libLLVMMC.a" >> merge.mri
    echo "ADDLIB ${LLVM_LIB_DIR}/libLLVMDebugInfoCodeView.a" >> merge.mri
    echo "ADDLIB ${LLVM_LIB_DIR}/libLLVMBinaryFormat.a" >> merge.mri
    echo "ADDLIB ${LLVM_LIB_DIR}/libLLVMSupport.a" >> merge.mri
    echo "ADDLIB ${LLVM_LIB_DIR}/libLLVMDemangle.a" >> merge.mri
    echo "ADDLIB ${LLVM_LIB_DIR}/libLLVMMCDisassembler.a" >> merge.mri
    echo "SAVE" >> merge.mri
    echo "END" >> merge.mri
    #执行合并
    ar -M < merge.mri
    rm merge.mri
    if [ ! -z libsym.a ];then
        rm libsym.a
    fi
    mv libsym_bak.a libsym.a
    if [ "${GO}" = "true" ];then
        cp libsym.a ${PROJECT_DIR}/go/src/libkperf/static_lib/
    fi
}

function build_test()
{
    if [ "$INCLUDE_TEST" = "true" ]; then
        execute_binary "$PROJECT_DIR"
    fi
}

main() {
    build_symbolizer
    build_elfin
    build_libkperf
    build_test
    merge_libsym
}

# bash build.sh test=true installPath=/home/ build_type=Release .The last three settings are optional.
main $@

