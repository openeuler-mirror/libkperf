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

cpu_core_num=$(($(nproc)-1))

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