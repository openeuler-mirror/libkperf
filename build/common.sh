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

# for instance.
# test_case_exclude =("TestCount.LLCacheMissRatio TestSPE.SpeProcCollectSubProc TestSPE.SpeProcCollectTwoThreads" "")
test_case_exclude=(
    ""
    ""
)

function set_hipa_exclude() {
	test_case_exclude=(
		"TestAPI.TestForkNewThread TestAPI.TestCpuFreqSampling TestCgroup.TestCgroupSampling \
		 TestCount.PwritevFile TestGroup.TestEvtGroupForkNewThread TestMetric.GetCpuFreq"
		""
	)
}

function get_cpu_exclude() {
	CPU_TYPE=""
    CPU_ID=$(cat /sys/devices/system/cpu/cpu0/regs/identification/midr_el1)
    echo "Detected CPU ID: $CPU_ID"
    case $CPU_ID in
        "0x00000000481fd010")
			CPU_TYPE="HIPA"
			set_hipa_exclude
			;;
		"0x00000000480fd020")
			CPU_TYPE="HIPB"
			;;
		"0x00000000480fd030")
			CPU_TYPE="HIPC"
			;;
		"0x00000000480fd220")
			CPU_TYPE="HIPF"
			;;
		"0x00000000480fd450")
			CPU_TYPE="HIPE"
			;;
		"0x00000000480fd060")
			CPU_TYPE="HIPG"
			;;
		*)
			CPU_TYPE="ELSE"
			;;
	esac
	echo "CPU_TYPE: $CPU_TYPE"
}

function execute_binary() {
    test_case_dir=("test_perf" "test_symbol")
    test_case_name=("test_perf" "test_symbol")
	get_cpu_exclude
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

function build_capstone() {
  local open_source_dir=$1
  local cmake_target_dir=$1/local/capstone
  if [ -d "${cmake_target_dir}" ];then
    return
  fi
  cd "$open_source_dir/capstone"
  mkdir -p build
  pushd build
  cmake -DCMAKE_INSTALL_PREFIX=$cmake_target_dir -DCMAKE_POSITION_INDEPENDENT_CODE=ON ..
  make -j ${cpu_core_num}
  make install
}

function build_java_trace() {
  echo "enable java trace compilation"
  local install_path=$1
  local root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
  local java_trace_dir="${root_dir}/java/java_trace"
  local trace_filter_config="${root_dir}/pmu/trace/trace_filter.conf"
  local trace_java_lib_dir="${install_path}/lib/java"
  local trace_conf_dir="${install_path}/conf"
  if [ ! -d "${java_trace_dir}" ]; then
    echo "ERROR: java trace directory not found: ${java_trace_dir}" >&2
    return 1
  fi
  mkdir -p "${trace_java_lib_dir}"
  mkdir -p "${trace_conf_dir}"
  pushd "${java_trace_dir}" >/dev/null || return 1
  local build_ok=1

  # try gradle
  echo "try to build java-trace with Gradle"
  local gradle_cmd="$(command -v gradle 2>/dev/null)"
  if [ -n "${gradle_cmd}" ]; then
    echo "using Gradle: ${gradle_cmd}"
    if "${gradle_cmd}" clean build -PlibkperfJavaOutDir="${trace_java_lib_dir}"; then
      build_ok=0
    else
      build_ok=$?
      echo "Gradle build failed, exit code: ${build_ok}"
    fi
  else
    echo "Gradle not found, skip Gradle build"
    build_ok=1
  fi

  # try maven
  if [ ${build_ok} -ne 0 ]; then
    echo "try to build java-trace with Maven"
    local maven_cmd="$(command -v mvn 2>/dev/null)"
    if [ -n "${maven_cmd}" ]; then
      echo "using Maven: ${maven_cmd}"
      if "${maven_cmd}" clean package -Dlibkperf.java.out.dir="${trace_java_lib_dir}"; then
        build_ok=0
      else
        build_ok=$?
        echo "Maven build failed, exit code: ${build_ok}"
      fi
    else
      echo "Maven not found, skip Maven build"
      build_ok=1
    fi
  fi

  popd >/dev/null || return 1
  if [ ${build_ok} -ne 0 ]; then
    echo "ERROR: java-trace build failed with both Gradle and Maven" >&2
    return 1
  fi

  if [ ! -f "${trace_java_lib_dir}/trace_agent.jar" ]; then
    echo "ERROR: missing ${trace_java_lib_dir}/trace_agent.jar" >&2
    return 1
  fi
  if [ ! -f "${trace_java_lib_dir}/trace_cli.jar" ]; then
    echo "ERROR: missing ${trace_java_lib_dir}/trace_cli.jar" >&2
    return 1
  fi

  # Install the shared trace filter configuration used by the Java backend.
  if [ -f "${trace_filter_config}" ]; then
    cp -f "${trace_filter_config}" "${trace_conf_dir}/trace_filter.conf"
    echo "trace_filter.conf copied to ${trace_conf_dir}/"
  else
    echo "WARNING: trace filter config not found: ${trace_filter_config}" >&2
  fi

  echo "java trace jars generated:"
  echo "  ${trace_java_lib_dir}/trace_agent.jar"
  echo "  ${trace_java_lib_dir}/trace_cli.jar"
}
