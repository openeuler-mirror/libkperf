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
# Description: Partial methods for building scripts.
set -e

export BPF_CLANG="clang"
export BPF_TOOL="bpftool"

cpu_core_num=$(($(nproc)-1))

if [ "$cpu_core_num" -eq 0 ];then
   cpu_core_num=1
fi

creat_dir(){
	local target_dir="$1"
	if [ -d "${target_dir}" ];then
		rm -rf ${target_dir}
	fi
	mkdir -p ${target_dir}
}

function build_googletest(){
  local open_source_dir=$1
  local cmake_target_dir=$1/local/googletest
  if [ -d "${cmake_target_dir}" ];then
    echo ${cmake_target_dir} "is exist"
    return
  else
    echo ${cmake_target_dir} "is not exist"
  fi
  pushd "$open_source_dir/googletest"
  mkdir -p build
  pushd build
  cmake -DCMAKE_INSTALL_PREFIX=$cmake_target_dir -DCMAKE_CXX_FLAGS="-fPIC" ..
  make -j ${cpu_core_num}
  make install
  echo "install log path: $cmake_target_dir"
}

function execute_binary() {
    test_case_dir=("test_perf" "test_symbol")
    test_case_name=("test_perf" "test_symbol")
    # for instance.
    # test_case_exclude =("TestCount.LLCacheMissRatio TestSPE.SpeProcCollectSubProc TestSPE.SpeProcCollectTwoThreads" "")
    test_case_exclude=(
        ""
        ""
    )
    test_prefix="$1"/_build/test/
    # 遍历数组
    for i in "${!test_case_dir[@]}"; do
        dir="${test_case_dir[$i]}"
        exe="${test_case_name[$i]}"
        cd "${test_prefix}${dir}"
        exclude="${test_case_exclude[$i]}"

        # 构建命令字符串
        command="LC_ALL=C ./$exe --gtest_filter=-"
        if [ -n "$exclude" ]; then
            for ex in $exclude; do
                command="$command$ex:"
            done
            # 移除末尾的冒号
            command=${command%:}
        fi

        # 执行命令
        echo "执行命令: $command"
        eval "$command"
    done
}

function build_libbpf() {
  local open_source_dir=$1
  local cmake_target_dir=$1/bpf
  if [ -d "${cmake_target_dir}" ];then
    echo ${cmake_target_dir} "is exist"
    return
  else
    echo ${cmake_target_dir} "is not exist"
  fi
  pushd "$open_source_dir/libbpf/src"
  make -j ${cpu_core_num}
  make install DESTDIR=$open_source_dir/local/bpf
  echo "install log path: $cmake_target_dir"
}

function build_skel_files() {
  command -v $BPF_CLANG &> /dev/null || error_exit "Error: $BPF_CLANG not found. Please install LLVM/Clang."
  command -v $BPF_TOOL &> /dev/null || error_exit "Error: $BPF_TOOL not found. Please install bpftool."
  
  local bpf_file_dir=$1
  local bpf_lib_dir=$2
  bpftool btf dump file /sys/kernel/btf/vmlinux format c > "${bpf_lib_dir}local/bpf/usr/include/bpf/vmlinux.h"
  if [ -s "${bpf_lib_dir}local/bpf/usr/include/bpf/vmlinux.h" ]; then
      echo "The kernel header file generated."
  else
      echo "Generate vmlinux.h file failed."
  fi

  for bpf_src in "${bpf_file_dir}"/*.bpf.c; do
    [ -f "$bpf_src" ] || continue
    src_name=$(basename "${bpf_src%.bpf.c}")
    obj_path="${bpf_file_dir}/${src_name}.bpf.o"
    skel_path="${bpf_file_dir}/${src_name}.skel.h"

    echo "compile: $src_name"
    clang -I${bpf_lib_dir}local/bpf/usr/include -g -O2 -target bpf -c "$bpf_src" -o "$obj_path"
    [ -s "$obj_path" ] || { echo "Error: The obj file was not generated."; exit 1; }
    bpftool gen skeleton "$obj_path" > "$skel_path"
    [ -s "$skel_path" ] || { echo "Error: The skeleton file was not generated."; exit 1; }
    grep -q 'struct bpf_prog' "$skel_path" || { echo "Error: invalid skeleton format."; exit 1; }
    echo "generate: ${src_name}.skel.h"
  done
}
