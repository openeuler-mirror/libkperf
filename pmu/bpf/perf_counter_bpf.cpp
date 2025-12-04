/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 * libkperf licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: Wu
 * Create: 2025-08-10
 * Description: implementations for reading performance counters and initializing counting logic
 * of PerfCounterBpf in the KUNPENG_PMU namespace.
 ******************************************************************************/
#include <climits>
#include <poll.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <cstring>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <linux/perf_event.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "pmu.h"
#include "linked_list.h"
#include "pfm_event.h"
#include "pmu_event.h"
#include "pcerr.h"
#include "log.h"
#include "sched_counter.skel.h"
#include "sched_cgroup.skel.h"
#include "perf_counter_bpf.h"

using namespace std;
using namespace pcerr;

#define MAX_ENTITES 102400
#define MAX_CPU_LIMIT 1024

static unordered_map<string, struct sched_counter_bpf *> counterMap; // key: evt name, value: bpf obj
static struct sched_cgroup_bpf *cgrpCounter = nullptr;               // one bpf obj in cgroup mode
static std::unordered_map<std::string, BpfEvent> evtDataMap;
static unordered_set<int> evtKeys;                                   // updated fds of cgroup
static unordered_set<string> readCgroups;
static unordered_set<string> triggerdEvt;
static int evtIdx = 0;
static int cgrpProgFd = 0;

static inline int TriggeredRead(int prog_fd, int cpu)
{
    // enforce the bpf trace function
    DECLARE_LIBBPF_OPTS(bpf_test_run_opts, opts,
                .ctx_in = NULL,                     // no input context
                .ctx_size_in = 0,
                .retval = 0,                        // return code of the BPF program
                .flags = BPF_F_TEST_RUN_ON_CPU,
                .cpu = cpu,
    );
    return bpf_prog_test_run_opts(prog_fd, &opts);
}

int KUNPENG_PMU::PerfCounterBpf::BeginRead()
{
    return SUCCESS;
}

int KUNPENG_PMU::PerfCounterBpf::EndRead()
{
    triggerdEvt.clear();
    readCgroups.clear();
    return SUCCESS;
}

inline int CachedCpuCount()
{
    static int cached = []{
        long n = sysconf(_SC_NPROCESSORS_CONF);
        return (n > 0) ? (int)n : 1;
    }();
    return cached > MAX_CPU_LIMIT ? MAX_CPU_LIMIT : cached;
}

int KUNPENG_PMU::PerfCounterBpf::ReadBpfProcess(const std::vector<int>& pids, std::vector<PmuData>& data)
{
    const unsigned cpuNums = CachedCpuCount();
    auto obj = counterMap[this->evt->name];

    // must execute sched_switch when each read operation.
    // the pid may not have been scheduled for a long time and the pmu count will not be recoreded.
    if (triggerdEvt.find(this->evt->name) == triggerdEvt.end()) {
        for (int i = 0; i < cpuNums; i++) {
            int triggerErr = TriggeredRead(evtDataMap[this->evt->name].bpfFd, i);
            if (triggerErr) {
                DBG_PRINT("trigger error: %s\n", strerror(-triggerErr));
            }
        }
        triggerdEvt.insert(this->evt->name);
    }

    static const std::vector<bpf_perf_event_value> zeros(cpuNums, bpf_perf_event_value{});
    for (int tid : pids) {
        std::vector<bpf_perf_event_value> values(cpuNums, bpf_perf_event_value{});
        if (bpf_map_lookup_elem(bpf_map__fd(obj->maps.accum_readings), &tid, values.data())) {
            continue;
        }

        int processId = 0;
        auto it = procMap.find(tid);
        if (it != procMap.end()) {
            processId = it->second->pid;
        }
        for (int cpu = 0; cpu < cpuNums; ++cpu) {
            if (values[cpu].counter == 0) continue;
            data.emplace_back(PmuData{
                .pid          = processId,
                .tid          = tid,
                .cpu          = cpu,
                .count        = values[cpu].counter,
                .countPercent = values[cpu].enabled ? 
                                (double)values[cpu].running / values[cpu].enabled : 0.0,
            });
        }

        bpf_map_update_elem(bpf_map__fd(obj->maps.accum_readings), &tid, zeros.data(), BPF_EXIST);
    }

    return SUCCESS;
}

int KUNPENG_PMU::PerfCounterBpf::ReadBpfCgroup(std::vector<PmuData> &data)
{
    auto cgrpName = this->evt->cgroupName;
    if (readCgroups.find(cgrpName) != readCgroups.end()) {
        return SUCCESS;
    }
    readCgroups.insert(cgrpName);

    for (int i = 0; i < MAX_CPU_NUM; ++i) {
        int triggerErr = TriggeredRead(cgrpProgFd, i);
        if (triggerErr) {
            DBG_PRINT("trigger error: %s\n", strerror(-triggerErr));
        }
    }

    const unsigned cpuNums = MAX_CPU_NUM;
    struct bpf_perf_event_value values[cpuNums];
    int readKey = cgroupIdxMap[cgrpName] * evtDataMap.size() + evtDataMap[this->evt->name].eventId;
    int err = bpf_map__lookup_elem(cgrpCounter->maps.cgrp_readings, &readKey, sizeof(__u32), values, sizeof(values), BPF_ANY);
    if (err) {
        string msg =
            "failed to lookup cgroup map cgrp_readings. Error: " + string(strerror(-err)) + " pid " + to_string(this->pid);
        New(LIBPERF_ERR_BPF_ACT_FAILED, msg);
        return SUCCESS;
    }

    for (int cpu = 0; cpu < cpuNums; ++cpu) {
        data.emplace_back(PmuData{
            .tid          = this->pid,
            .cpu          = cpu,
            .count        = values[cpu].counter,
            .countPercent = values[cpu].enabled ? 
                            (double)values[cpu].running / values[cpu].enabled : 0.0,
            .cgroupName   = this->evt->cgroupName.c_str(),
        });
    }

    memset(values, 0, cpuNums * sizeof(bpf_perf_event_value));
    err = bpf_map__update_elem(cgrpCounter->maps.cgrp_readings, &readKey, sizeof(__u32), values, sizeof(bpf_perf_event_value) * MAX_CPU_NUM, BPF_ANY);
    if (err) {
        New(LIBPERF_ERR_BPF_ACT_FAILED, "failed to update cgroup map cgrp_readings. Error: "
                + string(strerror(-err)) + " pid " + to_string(this->pid));
        return LIBPERF_ERR_BPF_ACT_FAILED;
    }
    return SUCCESS;
}

int KUNPENG_PMU::PerfCounterBpf::Read(EventData &eventData)
{
    return SUCCESS;
}

static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args)
{
    return vfprintf(stderr, format, args);
}

int KUNPENG_PMU::PerfCounterBpf::InitPidForEvent(const std::vector<int>& pids)
{
    if (!this->evt->cgroupName.empty()) {
        return SUCCESS;
    }

    auto findObj = counterMap.find(this->evt->name);
    if (findObj == counterMap.end()) {
        return -1;
    }
    auto obj = findObj->second;
    int map_fd = bpf_map__fd(obj->maps.accum_readings);
    // initialize the cumulative pmu count for this pid, only once
    int cpu_num = CachedCpuCount();

    std::vector<__u32> keys;
    keys.reserve(pids.size());
    for (int pid : pids) {
        keys.push_back(static_cast<__u32>(pid));
    }

    std::vector<bpf_perf_event_value> values(keys.size() * cpu_num, bpf_perf_event_value{});
    __u32 cnt = static_cast<__u32>(keys.size());
    struct bpf_map_batch_opts opts {
        .sz = sizeof(struct bpf_map_batch_opts),
        .flags = BPF_NOEXIST,
    };

    // initialize the cumulative pmu count for this pid
    int err = bpf_map_update_batch(map_fd, keys.data(), values.data(), &cnt, &opts);
    if (err) {
        // batch error, rollback to update elem one by one
        for (auto pid : keys) {
            std::vector<bpf_perf_event_value> zeroVals(cpu_num, bpf_perf_event_value{});
            int err = bpf_map__update_elem(obj->maps.accum_readings,
                                         &pid, sizeof(__u32),
                                         zeroVals.data(),
                                         sizeof(bpf_perf_event_value) * cpu_num,
                                         BPF_NOEXIST);
            if (err && err != -EEXIST) {
                New(LIBPERF_ERR_BPF_ACT_FAILED, "failed to update counter map accum_readings. Error: " + std::to_string(err));
                return LIBPERF_ERR_BPF_ACT_FAILED;
            }
        }
    }

    // initialize the filter, build the map relationship of pid and accum_key
    for (auto pid : keys) {
        int err = bpf_map__update_elem(obj->maps.filter,&pid, sizeof(__u32), &pid, sizeof(__u32), BPF_NOEXIST);
        if (err && err != -EEXIST) {
            New(LIBPERF_ERR_BPF_ACT_FAILED,"failed to update counter map filter. Error: " + std::to_string(err));
            return LIBPERF_ERR_BPF_ACT_FAILED;
        }
        DBG_PRINT("Init pid %d For eventId: %s\n", pid, this->evt->name.c_str());
        evtDataMap[this->evt->name].pids.insert(pid);
    }

    return SUCCESS;
}

int KUNPENG_PMU::PerfCounterBpf::InitBpfObj()
{
    int err;
    struct sched_counter_bpf *obj;

    auto findObj = counterMap[this->evt->name];
    if (!findObj) {
        // initialize the bpf obj
        obj = sched_counter_bpf__open();
        if (!obj) {
            New(LIBPERF_ERR_BPF_ACT_FAILED, "failed to open counter bpf obj");
            return LIBPERF_ERR_BPF_ACT_FAILED;
        }
        err = bpf_map__set_max_entries(obj->maps.events, MAX_CPU_NUM);
        if (err) {
            New(LIBPERF_ERR_BPF_ACT_FAILED, "failed to set max entries of counter map: events");
            return LIBPERF_ERR_BPF_ACT_FAILED;
        }
        err = bpf_map__set_max_entries(obj->maps.prev_readings, 1);
        if (err) {
            New(LIBPERF_ERR_BPF_ACT_FAILED, "failed to set max entries of counter map: prev_readings");
            return LIBPERF_ERR_BPF_ACT_FAILED;
        }
        err = bpf_map__set_max_entries(obj->maps.accum_readings, this->procMap.size());
        if (err) {
            New(LIBPERF_ERR_BPF_ACT_FAILED, "failed to set max entries of counter map: accum_readings");
            return LIBPERF_ERR_BPF_ACT_FAILED;
        }
        err = bpf_map__set_max_entries(obj->maps.filter, MAX_ENTITES);
        if (err) {
            New(LIBPERF_ERR_BPF_ACT_FAILED, "failed to set max entries of counter map: filter");
            return LIBPERF_ERR_BPF_ACT_FAILED;
        }

        err = sched_counter_bpf__load(obj);
        if (err) {
            New(LIBPERF_ERR_BPF_ACT_FAILED, "failed to load counter bpf obj");
            return LIBPERF_ERR_BPF_ACT_FAILED;
        }

        err = sched_counter_bpf__attach(obj);
        if (err) {
            New(LIBPERF_ERR_BPF_ACT_FAILED, "failed to attach counter bpf obj");
            return LIBPERF_ERR_BPF_ACT_FAILED;
        }

        counterMap[this->evt->name] = obj;

        // get the fd of bpf prog, trigger trace function(sched_switch) of bpf in read
        int progFd = bpf_program__fd(obj->progs.on_switch);
        evtDataMap[this->evt->name].bpfFd = progFd;
        DBG_PRINT("create bpf obj for evt %s prog fd %d\n", evt->name.c_str(), progFd);
    } else {
        obj = findObj;
    }

    // initialize the pmu count, put fd of pmu into value
    err = bpf_map__update_elem(obj->maps.events, &this->cpu, sizeof(__u32), &this->fd, sizeof(int), BPF_ANY);
    if (err) {
        New(LIBPERF_ERR_BPF_ACT_FAILED, "failed to update counter map events. Error: "
                 + string(strerror(-err)) + "cpu " + to_string(cpu) + "fd " + to_string(fd));
        return LIBPERF_ERR_BPF_ACT_FAILED;
    }

    evtDataMap[this->evt->name].cpus.insert(this->cpu);
    return SUCCESS;
}

static uint64_t ReadCgroupId(const string &cgroupName)
{
    char path[PATH_MAX + 1];
    char mnt[PATH_MAX + 1];
    struct {
        struct file_handle fh;
        uint64_t cgroup_id;
    } handle;
    int mount_id;
    std::string fullCgroupPath = GetCgroupPath(cgroupName);
    handle.fh.handle_bytes = sizeof(handle.cgroup_id);
    if (name_to_handle_at(AT_FDCWD, fullCgroupPath.c_str(), &handle.fh, &mount_id, 0) < 0) {
        return -1;
    }

    return handle.cgroup_id;
}

int KUNPENG_PMU::PerfCounterBpf::InitBpfCgroupObj()
{
    int err;
    struct sched_cgroup_bpf *obj;
    if (cgrpCounter == nullptr) {
        obj = sched_cgroup_bpf__open();
        if(!obj){
            New(LIBPERF_ERR_BPF_ACT_FAILED, "failed to open cgroup bpf obj");
            return LIBPERF_ERR_BPF_ACT_FAILED;
        }

        obj->rodata->num_cpus = MAX_CPU_NUM;
        obj->rodata->num_events = this->evt->numEvent;

        err = bpf_map__set_max_entries(obj->maps.events, MAX_ENTITES);
        if (err) {
            New(LIBPERF_ERR_BPF_ACT_FAILED, "failed to set max entries of cgroup map: events");
            return LIBPERF_ERR_BPF_ACT_FAILED;
        }

        err = bpf_map__set_max_entries(obj->maps.prev_readings, MAX_ENTITES);
        if (err) {
            New(LIBPERF_ERR_BPF_ACT_FAILED, "failed to set max entries of cgroup map: prev_readings");
            return LIBPERF_ERR_BPF_ACT_FAILED;
        }

        err = bpf_map__set_max_entries(obj->maps.cgrp_idx, MAX_ENTITES * 100);
        if (err) {
            New(LIBPERF_ERR_BPF_ACT_FAILED, "failed to set max entries of cgroup map: cgrp_idx");
            return LIBPERF_ERR_BPF_ACT_FAILED;
        }

        err = bpf_map__set_max_entries(obj->maps.cgrp_readings, MAX_ENTITES);
        if (err) {
            New(LIBPERF_ERR_BPF_ACT_FAILED, "failed to set max entries of cgroup map: cgrp_readings");
            return LIBPERF_ERR_BPF_ACT_FAILED;
        }

        err = sched_cgroup_bpf__load(obj);
        if (err) {
            New(LIBPERF_ERR_BPF_ACT_FAILED, "failed to load cgroup bpf obj");
            return LIBPERF_ERR_BPF_ACT_FAILED;
        }

        err = sched_cgroup_bpf__attach(obj);
        if (err) {
            New(LIBPERF_ERR_BPF_ACT_FAILED, "failed to attach cgroup bpf obj");
            return LIBPERF_ERR_BPF_ACT_FAILED;
        }

        cgrpProgFd = bpf_program__fd(obj->progs.trigger_read);
        cgrpCounter = obj;
        DBG_PRINT("create bpf obj for cgroup evt %s \n", evt->name.c_str());
    }

    auto findEvtIdx = evtDataMap.find(this->evt->name);
    if (findEvtIdx == evtDataMap.end()) {
        evtDataMap[this->evt->name].eventId = evtIdx;
        evtIdx++;
    }
    int evtKey = evtDataMap[this->evt->name].eventId * MAX_CPU_NUM + cpu;
    if (evtKeys.find(evtKey) == evtKeys.end()) {
        err = bpf_map__update_elem(cgrpCounter->maps.events, &evtKey, sizeof(__u32),
                                    &this->fd, sizeof(int), BPF_ANY);
        if (err) {
            New(LIBPERF_ERR_BPF_ACT_FAILED, "failed to update cgroup map events. Error: "
                 + string(strerror(-err)) + "cpu " + to_string(cpu) + "fd " + to_string(fd));
            return LIBPERF_ERR_BPF_ACT_FAILED;
        }
        evtKeys.insert(evtKey);
    }

    string cgrpName = this->evt->cgroupName;
    auto findCgrp = cgroupIdxMap.find(cgrpName);
    if (findCgrp == cgroupIdxMap.end()) {
        uint64_t cgrpId = ReadCgroupId(cgrpName);
        if (cgrpId == UINT64_MAX) {
            New(LIBPERF_ERR_BPF_ACT_FAILED, "failed to get cgroup id of: " + cgrpName);
            return LIBPERF_ERR_BPF_ACT_FAILED;
        }
        int cgrpIdx = cgroupIdxMap.size();
        err = bpf_map__update_elem(cgrpCounter->maps.cgrp_idx, &cgrpId, sizeof(__u64), &cgrpIdx, sizeof(__u32), BPF_ANY);
        if (err) {
            New(LIBPERF_ERR_BPF_ACT_FAILED, "failed to update cgroup id: " + cgrpId);
            return LIBPERF_ERR_BPF_ACT_FAILED;
        }
        DBG_PRINT("init cgroup bpf map: %s id: %d\n", cgrpName.c_str(), cgrpId);
        cgroupIdxMap[cgrpName] = cgrpIdx;
    }

    evtDataMap[this->evt->name].cpus.insert(this->cpu);
    return SUCCESS;
}

int KUNPENG_PMU::PerfCounterBpf::Init(const bool groupEnable, const int groupFd, const int resetOutputFd)
{
    auto findCpuMap = evtDataMap.find(this->evt->name);
    auto findCgroup = cgroupIdxMap.find(this->evt->cgroupName);
    if (findCpuMap != evtDataMap.end() && findCpuMap->second.cpus.count(this->cpu) && findCgroup != cgroupIdxMap.end()) {
        return SUCCESS;
    }

    if (findCpuMap == evtDataMap.end() || !findCpuMap->second.cpus.count(this->cpu)) {
        int err = this->MapPerfAttr(groupEnable, groupFd);
        if (err != SUCCESS) {
            return err;
        }
    }

    return this->evt->cgroupName.empty() ? InitBpfObj() : InitBpfCgroupObj();
}

int KUNPENG_PMU::PerfCounterBpf::MapPerfAttr(const bool groupEnable, const int groupFd)
{
    struct perf_event_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.size = sizeof(struct perf_event_attr);
    attr.type = this->evt->type;
    attr.config = this->evt->config;
    attr.config1 = this->evt->config1;
    attr.config2 = this->evt->config2;
    attr.disabled = 1;

    // support cgroup feature
    unsigned flags = 0;
    if (this->GetCgroupFd() != -1) {
        flags = PERF_FLAG_PID_CGROUP | PERF_FLAG_FD_CLOEXEC;
        this->pid = this->GetCgroupFd();
    }

    attr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING | PERF_FORMAT_ID;

    this->fd = PerfEventOpen(&attr, -1, this->cpu, groupFd, 0);
    DBG_PRINT("type: %d cpu: %d config: %llx config1: %llx config2: %llx myfd: %d groupfd: %d\n",
        attr.type, cpu, attr.config, attr.config1, attr.config2, this->fd, groupFd);
    if (__glibc_unlikely(this->fd < 0)) {
        return MapErrno(errno);
    }
    return SUCCESS;
}

int KUNPENG_PMU::PerfCounterBpf::Enable()
{
    int err = PerfEvt::Enable();
    if (err != SUCCESS) {
        return err;
    }
    return SUCCESS;
}

int KUNPENG_PMU::PerfCounterBpf::Disable()
{
    return PerfEvt::Disable();
}

int KUNPENG_PMU::PerfCounterBpf::Reset()
{
    return PerfEvt::Reset();
}

int KUNPENG_PMU::PerfCounterBpf::Close()
{
    if (this->fd > 0) {
        close(this->fd);
    }
    return SUCCESS;
}