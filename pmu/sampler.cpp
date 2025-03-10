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
#include "trace_pointer_parser.h"
#include "common.h"

using namespace std;

static constexpr int PAGE_SIZE = 4096;
static constexpr int MAX_ATTR_SIZE = 120;
int KUNPENG_PMU::PerfSampler::pages = 128;

template <typename T>
static inline bool IsPowerOfTwo(T x)
{
    return (x) != 0 && (((x) & ((x) - 1)) == 0);
}

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
    attr.pinned = 1;
    attr.disabled = 1;
    attr.inherit = 1;
    attr.mmap = 1;
    attr.comm = 1;
    attr.mmap2 = 1;
    attr.task = 1;
    attr.sample_id_all = 1;
    attr.exclude_guest = 1;

    // if exist event group, adapte the child events config parameters
    if (groupEnable) {
        attr.pinned = 0;
        attr.disabled = 0;
    }        

    this->fd = PerfEventOpen(&attr, this->pid, this->cpu, groupFd, 0);
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
    int mmapLen = (this->pages + 1) * PAGE_SIZE;
    auto mask = mmapLen - PAGE_SIZE - 1;
    if (mask < 0) {
        return UNKNOWN_ERROR;
    }

    this->sampleMmap->prev = 0;
    this->sampleMmap->mask = static_cast<__u64>(mask);
    void *currentMap =
            mmap(NULL, this->sampleMmap->mask + 1 + PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
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
    if (this->sampleMmap->base && this->sampleMmap->base != MAP_FAILED) {
        munmap(this->sampleMmap->base, this->sampleMmap->mask + 1 + PAGE_SIZE);
    }
    this->sampleMmap->base = nullptr;
    if (this->sampleMmap->fd > 0) {
        close(this->sampleMmap->fd);
    }
    return SUCCESS;
}

int KUNPENG_PMU::PerfSampler::ReadInit()
{
    if (!this->sampleMmap->base) {
        return UNKNOWN_ERROR;
    }
    return RingbufferReadInit(*this->sampleMmap.get());
}

void KUNPENG_PMU::PerfSampler::UpdatePidInfo(const pid_t &pid, const int &tid)
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
        strcpy(procTopo->comm, event->comm.comm);
        DBG_PRINT("Add to proc map: %d\n", event->comm.tid);
        procMap[event->comm.tid] = procTopo;
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
    if (symMode != NO_SYMBOL_RESOLVE) {
        // Copy ips from ring buffer and get stack info later.
        if (evt->callStack == 0) {
            int i = 0;
            while (i < sample->nr && !IsValidIp(sample->ips[i])) {
                i++;
            }
            if (i < sample->nr) {
                ips->ips.push_back(sample->ips[i]);
            }
        } else {
            for (int i = sample->nr - 1; i >= 0; --i) {
                if (IsValidIp(sample->ips[i])) {
                    ips->ips.push_back(sample->ips[i]);
                }
            }
        }
    }
    current->cpu = static_cast<unsigned>(sample->cpu);
    current->pid = static_cast<pid_t>(sample->pid);
    current->tid = static_cast<int>(sample->tid);
    current->period = static_cast<uint64_t>(sample->period);
    current->ts = static_cast<int64_t>(sample->time);
    PointerPasser::ParserRawFormatData(current, sample, event, this->evt->name);
    ParseBranchSampleData(current, sample, event, extPool);
}

void KUNPENG_PMU::PerfSampler::ReadRingBuffer(vector<PmuData> &data, vector<PerfSampleIps> &sampleIps, std::vector<PmuDataExt*> &extPool)
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
                if (symMode == RESOLVE_ELF_DWARF) {
                    SymResolverUpdateModule(event->mmap.tid, event->mmap.filename, event->mmap.addr);
                } else if (symMode == RESOLVE_ELF) {
                    SymResolverUpdateModuleNoDwarf(event->mmap.tid, event->mmap.filename, event->mmap.addr);
                }
                break;
            }
            case PERF_RECORD_MMAP2: {
                if (symMode == RESOLVE_ELF_DWARF) {
                    SymResolverUpdateModule(event->mmap2.tid, event->mmap2.filename, event->mmap2.addr);
                } else if (symMode == RESOLVE_ELF) {
                    SymResolverUpdateModuleNoDwarf(event->mmap2.tid, event->mmap2.filename, event->mmap2.addr);
                }
                break;
            }
            case PERF_RECORD_FORK: {
                DBG_PRINT("Fork ptid: %d tid: %d\n", event->fork.pid, event->fork.tid);
                UpdatePidInfo(event->fork.pid, event->fork.tid);
                break;
            }
            case PERF_RECORD_COMM: {
                UpdateCommInfo(event);
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
            UpdatePidInfo(pmuData.pid, pmuData.tid);
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

int KUNPENG_PMU::PerfSampler::Read(vector<PmuData> &data, std::vector<PerfSampleIps> &sampleIps, std::vector<PmuDataExt*> &extPool)
{
    auto err = this->ReadInit();
    if (__glibc_unlikely(err != SUCCESS)) {
        return err;
    }
    auto cnt = data.size();
    this->ReadRingBuffer(data, sampleIps, extPool);
    if (this->pid == -1) {
        FillComm(cnt, data.size(), data);
    }

    return SUCCESS;
}

int KUNPENG_PMU::PerfSampler::Init(const bool groupEnable, const int groupFd)
{
    auto err = this->MapPerfAttr(groupEnable, groupFd);
    if (err != SUCCESS) {
        return err;
    }
    err = this->Mmap();
    if (__glibc_unlikely(err != SUCCESS)) {
        close(this->fd);
        return LIBPERF_ERR_FAIL_MMAP;
    }
    return SUCCESS;
}
