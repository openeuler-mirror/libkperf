/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
 * libkperf licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: Mr.Gan
 * Create: 2024-04-03
 * Description: implementations for sampling and processing performance data using ring buffers in
 * the KUNPENG_PMU namespace
 ******************************************************************************/
#include <climits>
#include <iostream>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <cstring>
#include <linux/perf_event.h>
#include "linked_list.h"
#include "symbol_resolve.h"
#include "util_time.h"
#include "sample_process.h"
#include "pcerrc.h"
#include "process_map.h"
#include "log.h"
#include "sampler.h"
#include "pfm_event.h"
#include "trace_point_parser.h"
#include "common.h"

using namespace std;

int KUNPENG_PMU::PerfSampler::MapPerfAttr(const bool groupEnable, const int groupFd)
{
    struct perf_event_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.type = this->evt->type;
    attr.config = this->evt->config;
    attr.size = sizeof(struct perf_event_attr);
    attr.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_TIME | PERF_SAMPLE_CALLCHAIN | PERF_SAMPLE_ID |
                       PERF_SAMPLE_CPU | PERF_SAMPLE_PERIOD | PERF_SAMPLE_IDENTIFIER | PERF_SAMPLE_RAW;
    // if the branch sample type is not nullptr, set the branch sample type.                       
    if (branchSampleFilter != KPERF_NO_BRANCH_SAMPLE) {
        attr.sample_type |= PERF_SAMPLE_BRANCH_STACK;
        attr.branch_sample_type = branchSampleFilter;
    }
    attr.freq = this->evt->useFreq;
    attr.sample_period = this->evt->period;
    attr.read_format = PERF_FORMAT_ID;
    attr.exclude_kernel = this->evt->excludeKernel;
    attr.exclude_user = this->evt->excludeUser;
#ifdef IS_X86
    if (this->pid == -1) {
        attr.pinned = 0;
    }
#else
    attr.pinned = 1;
#endif
    attr.disabled = 1;
    attr.inherit = 1;
    attr.mmap = 1;
    attr.comm = 1;
    attr.mmap2 = 1;
    attr.task = 1;
    attr.sample_id_all = 1;
    attr.exclude_guest = 1;
    if ((this->evt->blockedSample == 1) && (this->evt->name == "context-switches")) {
        attr.exclude_kernel = 0; // for confrim the reason of entering off cpu, it need to include kernel.
        attr.context_switch = 1;
        attr.freq = 0;
        attr.sample_period = 1; 
    }

    // if exist event group, adapte the child events config parameters
    if (groupEnable) {
        attr.pinned = 0;
        attr.disabled = 0;
    }
    unsigned flags = 0;
    if (this->GetCgroupFd() != -1) {
        flags = PERF_FLAG_PID_CGROUP | PERF_FLAG_FD_CLOEXEC;
        this->pid = this->GetCgroupFd();
    }

    this->fd = PerfEventOpen(&attr, this->pid, this->cpu, groupFd, flags);
    DBG_PRINT("pid: %d type: %d cpu: %d config: %X myfd: %d groupfd: %d\n", this->pid, attr.type, cpu, attr.config, this->fd, groupFd);
    if (__glibc_unlikely(this->fd < 0)) {
        return MapErrno(errno);
    }
    return SUCCESS;
}

union KUNPENG_PMU::PerfEvent *KUNPENG_PMU::PerfSampler::SampleReadEvent()
{
    return ReadEvent(*this->sampleMmap);
}

int KUNPENG_PMU::PerfSampler::Mmap()
{
    int mmapLen = (SAMPLE_PAGES + 1) * SAMPLE_PAGE_SIZE;
    auto mask = mmapLen - SAMPLE_PAGE_SIZE - 1;
    if (mask < 0) {
        return UNKNOWN_ERROR;
    }

    this->sampleMmap->prev = 0;
    this->sampleMmap->mask = static_cast<__u64>(mask);
    void *currentMap =
            mmap(NULL, this->sampleMmap->mask + 1 + SAMPLE_PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (__glibc_unlikely(currentMap == MAP_FAILED)) {
        this->sampleMmap->base = nullptr;
        return UNKNOWN_ERROR;
    }
    this->sampleMmap->base = static_cast<struct perf_event_mmap_page *>(currentMap);
    this->sampleMmap->fd = fd;
    return SUCCESS;
}

int KUNPENG_PMU::PerfSampler::Close()
{
    if (this->sampleMmap && this->sampleMmap->base && this->sampleMmap->base != MAP_FAILED) {
        munmap(this->sampleMmap->base, this->sampleMmap->mask + 1 + SAMPLE_PAGE_SIZE);
    }
    if (this->fd > 0) {
        close(this->fd);
    }
    return SUCCESS;
}

void KUNPENG_PMU::PerfSampler::UpdatePidInfo(const int &tid)
{
    auto findProc = procMap.find(tid);
    if (findProc == procMap.end()) {
        auto procTopo = GetProcTopology(tid);
        if (procTopo != nullptr) {
            DBG_PRINT("Add to proc map: %d\n", tid);
            procMap[tid] = shared_ptr<ProcTopology>(procTopo, FreeProcTopo);
        }
    }
}

void KUNPENG_PMU::PerfSampler::UpdateCommInfo(KUNPENG_PMU::PerfEvent *event)
{
    auto findProc = procMap.find(event->comm.tid);
    if (findProc == procMap.end()) {
        std::shared_ptr<ProcTopology> procTopo(new ProcTopology{0}, FreeProcTopo);
        procTopo->tid = event->comm.tid;
        procTopo->pid = event->comm.pid;
        procTopo->comm = static_cast<char *>(malloc(strlen(event->comm.comm) + 1));
        if (procTopo->comm == nullptr) {
            return;
        }
        strcpy(procTopo->comm, event->comm.comm);
        DBG_PRINT("Add to proc map: %d\n", event->comm.tid);
        procMap[event->comm.tid] = procTopo;
    }
}

void KUNPENG_PMU::PerfSampler::ParseSwitch(KUNPENG_PMU::PerfEvent *event, struct PmuSwitchData *switchCurData)
{
    if (switchCurData == nullptr) {
        return;
    }
    bool out = (event->header.misc & PERF_RECORD_MISC_SWITCH_OUT);
    
    if (event->header.type == PERF_RECORD_SWITCH) {
        KUNPENG_PMU::ContextSwitchEvent *switchEvent = (KUNPENG_PMU::ContextSwitchEvent *)event;
        switchCurData->pid = switchEvent->sampleId.pid;
        switchCurData->tid = switchEvent->sampleId.tid;
        switchCurData->ts = switchEvent->sampleId.time;
        switchCurData->cpu = switchEvent->sampleId.cpu;
        switchCurData->swOut = !out ? 0 : 1;
    }
}

void KUNPENG_PMU::PerfSampler::ParseBranchSampleData(struct PmuData *pmuData, PerfRawSample *sample, union PerfEvent *event, std::vector<PmuDataExt*> &extPool)
{
    if (branchSampleFilter == KPERF_NO_BRANCH_SAMPLE) {
        return;
    }

    auto ipsOffset = (unsigned long)offset(struct PerfRawSample, ips);
    auto traceOffset = ipsOffset + sample->nr * (sizeof(unsigned long));
    auto *traceData = (TraceRawData *)((char *)&event->sample.array + traceOffset);
    if (traceData == nullptr) {
        return;
    }

    auto branchOffset = traceOffset + sizeof(unsigned int) + sizeof(char) * traceData->size;
    auto branchData = (BranchSampleData *)((char *)&event->sample.array + branchOffset);
    if (branchData->bnr == 0) {
        return;
    }

    try {
        auto *branchExt = new struct PmuDataExt();
        auto *records = new struct BranchSampleRecord[branchData->bnr];
        for (int i = 0; i < branchData->bnr; i++) {
            auto branchItem = branchData->lbr[i];
            records[i].fromAddr = branchItem.from;
            records[i].toAddr = branchItem.to;
            records[i].cycles = branchItem.cycles;
            records[i].misPred = branchItem.mispred;
            records[i].predicted = branchItem.predicted;
        }
        branchExt->nr = branchData->bnr;
        branchExt->branchRecords = records;
        extPool.push_back(branchExt);
        pmuData->ext = branchExt;
    } catch (std::bad_alloc &err) {
        return;
    }
}


void KUNPENG_PMU::PerfSampler::RawSampleProcess(
        struct PmuData *current, PerfSampleIps *ips, union KUNPENG_PMU::PerfEvent *event, std::vector<PmuDataExt*> &extPool)
{
    if (current == nullptr) {
        return;
    }
    KUNPENG_PMU::PerfRawSample *sample = (KUNPENG_PMU::PerfRawSample *)event->sample.array;
    ips->ips.reserve(ips->ips.size() + sample->nr);
    // Copy ips from ring buffer and get stack info later.
    if (evt->callStack == 0) {
        int i = 0;
        while (i < sample->nr && !IsValidIp(sample->ips[i])) {
            i++;
        }
        if (i < sample->nr) {
            ips->ips.emplace_back(sample->ips[i]);
        }
    } else {
        for (int i = sample->nr - 1; i >= 0; --i) {
            const auto& ip = sample->ips[i];
            if (IsValidIp(ip)) {
                ips->ips.emplace_back(ip);
            }
        }
    }
    current->cpu = sample->cpu;
    current->pid = static_cast<pid_t>(sample->pid);
    current->tid = static_cast<int>(sample->tid);
    current->period = static_cast<uint64_t>(sample->period);
    current->ts = static_cast<int64_t>(sample->time);
    if (this->evt->pmuType == TRACE_TYPE) {
        TraceParser::ParserRawFormatData(current, sample, event, this->evt->name);
    }
    ParseBranchSampleData(current, sample, event, extPool);
    if (this->evt->cgroupName.size() != 0) {
        current->cgroupName = this->evt->cgroupName.c_str();
    }
}

void KUNPENG_PMU::PerfSampler::ReadRingBuffer(vector<PmuData> &data, vector<PerfSampleIps> &sampleIps,
    std::vector<PmuDataExt*> &extPool, std::vector<PmuSwitchData> &switchData)
{
    union KUNPENG_PMU::PerfEvent *event;
    while (true) {
        event = this->SampleReadEvent();
        if (__glibc_unlikely(event == nullptr)) {
            break;
        }
        __u32 sampleType = event->header.type;
        switch (sampleType) {
            case PERF_RECORD_SAMPLE: {
                data.emplace_back(PmuData{0});
                auto& current = data.back();
                sampleIps.emplace_back(PerfSampleIps());
                auto& ips = sampleIps.back();
                this->RawSampleProcess(&current, &ips, event, extPool);
                break;
            }
            case PERF_RECORD_MMAP: {
                if (symMode == RESOLVE_ELF_DWARF || symMode == NO_SYMBOL_RESOLVE) {
                    SymResolverUpdateModule(event->mmap.tid, event->mmap.filename, event->mmap.addr);
                } else if (symMode == RESOLVE_ELF) {
                    SymResolverUpdateModuleNoDwarf(event->mmap.tid, event->mmap.filename, event->mmap.addr);
                }
                break;
            }
            case PERF_RECORD_MMAP2: {
                if (symMode == RESOLVE_ELF_DWARF || symMode == NO_SYMBOL_RESOLVE) {
                    SymResolverUpdateModule(event->mmap2.tid, event->mmap2.filename, event->mmap2.addr);
                } else if (symMode == RESOLVE_ELF) {
                    SymResolverUpdateModuleNoDwarf(event->mmap2.tid, event->mmap2.filename, event->mmap2.addr);
                }
                break;
            }
            case PERF_RECORD_FORK: {
                DBG_PRINT("Fork ptid: %d tid: %d\n", event->fork.pid, event->fork.tid);
                UpdatePidInfo(event->fork.tid);
                break;
            }
            case PERF_RECORD_COMM: {
                UpdateCommInfo(event);
                break;
            }
            case PERF_RECORD_SWITCH: {
                switchData.emplace_back(PmuSwitchData{0});
                auto& switchCurData = switchData.back();
                ParseSwitch(event, &switchCurData);
                break;
            }
            default:
                break;
        }
        PerfMmapConsume(*this->sampleMmap);
    }
    PerfMmapReadDone(*this->sampleMmap);
}

void KUNPENG_PMU::PerfSampler::FillComm(const size_t &start, const size_t &end, vector<PmuData> &data)
{
    for (size_t i = start; i < end; ++i) {
        auto& pmuData = data[i];
        auto findProc = procMap.find(pmuData.tid);
        if (findProc == procMap.end()) {
            UpdatePidInfo(pmuData.tid);
            findProc = procMap.find(pmuData.tid);
            if (findProc == procMap.end()) {
                continue;
            }
            pmuData.comm = findProc->second->comm;
            continue;
        }
        pmuData.comm = findProc->second->comm;
    }
}

int KUNPENG_PMU::PerfSampler::Read(vector<PmuData> &data, std::vector<PerfSampleIps> &sampleIps,
    std::vector<PmuDataExt*> &extPool, std::vector<PmuSwitchData> &switchData)
{
    // This may be a lack of space.
    if(!this->sampleMmap || !this->sampleMmap->base) {
        return SUCCESS;
    }
    auto err =  RingbufferReadInit(*this->sampleMmap.get());
    if (__glibc_unlikely(err != SUCCESS)) {
        return err;
    }
    auto cnt = data.size();
    this->ReadRingBuffer(data, sampleIps, extPool, switchData);
    if (this->pid == -1) {
        FillComm(cnt, data.size(), data);
    }
                                                                
    return SUCCESS;
}

int KUNPENG_PMU::PerfSampler::MmapNormal() {
    this->sampleMmap = std::make_shared<PerfMmap>();
    int err = this->Mmap();
    if (__glibc_unlikely(err != SUCCESS)) {
        close(this->fd);
        return LIBPERF_ERR_FAIL_MMAP;
    }
    return SUCCESS;
}

int KUNPENG_PMU::PerfSampler::MmapResetOutput(const int resetOutputFd)
{
    if (ioctl(fd, PERF_EVENT_IOC_SET_OUTPUT, resetOutputFd) != 0) {
        return LIBPERF_ERR_RESET_FD;
    }
    if (fcntl(fd, F_SETFL, O_RDONLY | O_NONBLOCK) != 0) {
        return LIBPERF_ERR_SET_FD_RDONLY_NONBLOCK;
    }
    return SUCCESS;
}

int KUNPENG_PMU::PerfSampler::Init(const bool groupEnable, const int groupFd, const int resetOutputFd)
{
    auto err = this->MapPerfAttr(groupEnable, groupFd);
    if (err != SUCCESS) {
        return err;
    }
    if (resetOutputFd > 0) {
         return MmapResetOutput(resetOutputFd);
    }
    return MmapNormal();
}
