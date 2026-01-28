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
 * Author: Wu
 * Create: 2026-01-23
 * Description: declaration of pebs collection functions
 ******************************************************************************/
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <cstdint>
#include <algorithm>
#include <new>

#include "pcerrc.h"
#include "pcerr.h"
#include "pmu_list.h"
#include "pmu_event.h"
#include "cpu_map.h"
#include "simple_pebs_backend.h"

static inline bool IsValidAddr(uint64_t a)
{
    if (a == 0 || a == ~0ull) {
        return false;
    }
    return ((a >> 63) == 0);
}

bool PebsDriverManager::ProbeDevice(int* out_size)
{
    if (access("/dev/simple-pebs", R_OK) != 0) {
        return false;
    }

    int fd = open("/dev/simple-pebs", O_RDONLY);
    if (fd < 0) {
        return false;
    }

    int size = 0;
    if (ioctl(fd, SIMPLE_PEBS_GET_SIZE, &size) < 0 || size <= 0) {
        close(fd);
        return false;
    }
    close(fd);
    *out_size = size;
    return true;
}

bool PebsDriverManager::OpenCpu(PebsDriverSession& s, int cpu)
{
    int fd = open("/dev/simple-pebs", O_RDONLY);
    if (fd < 0) {
        return false;
    }

    if (ioctl(fd, SIMPLE_PEBS_SET_CPU, cpu) < 0) {
        close(fd);
        return false;
    }

    if (ioctl(fd, SIMPLE_PEBS_RESET, 0) < 0) {
        close(fd);
        return false;
    }

    void* addr = mmap(nullptr, s.driverMapSize, PROT_READ, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        close(fd);
        return false;
    }

    s.fds[cpu]  = fd;
    s.maps[cpu] = addr;
    s.pfds[cpu].fd = fd;
    s.pfds[cpu].events = POLLIN;
    s.pfds[cpu].revents = 0;
    return true;
}

void PebsDriverManager::StopAll(PebsDriverSession& s)
{
    for (int cpu = 0; cpu < s.cpuNum; cpu++) {
        if (s.fds[cpu] >= 0) {
            ioctl(s.fds[cpu], SIMPLE_PEBS_STOP, 0);
        }
    }
    s.running = false;
}

void PebsDriverManager::ResetAll(PebsDriverSession& s)
{
    for (int cpu = 0; cpu < s.cpuNum; cpu++) {
        if (s.fds[cpu] >= 0) {
            ioctl(s.fds[cpu], SIMPLE_PEBS_RESET, 0);
        }
    }
}

int PebsDriverManager::Open(struct PmuAttr *attr)
{
    // future: get the attr of PmuAttr to filter
    int size = 0;
    if (!ProbeDevice(&size)) {
        pcerr::New(LIBPERF_ERR_LBR_DRIVER_INVALID, "Probe pebs driver failed");
        return -1;
    }

    PebsDriverSession s;
    s.driverMapSize = size;
    s.cpuNum = MAX_CPU_NUM;
    s.fds.assign(s.cpuNum, -1);
    s.maps.assign(s.cpuNum, MAP_FAILED);
    s.pfds.assign(s.cpuNum, pollfd{.fd=-1, .events=POLLIN, .revents=0});
    s.running = false;

    for (int cpu = 0; cpu < s.cpuNum; cpu++) {
        (void)OpenCpu(s, cpu);  // if some cpus fail, continue
    }

    int pd = nextDriverPd.fetch_add(1);
    {
        std::lock_guard<std::mutex> lk(driverMu);
        sess.emplace(pd, std::move(s));
    }
    return pd;
}

int PebsDriverManager::Enable(int pd)
{
    std::lock_guard<std::mutex> lk(driverMu);
    auto it = sess.find(pd);
    if (it == sess.end()) {
        return -1;
    }
    auto& s = it->second;

    ResetAll(s);
    
    int failed = 0;
    int opened = 0;
    std::vector<int> failedCpus;
    failedCpus.reserve(s.cpuNum);

    for (int cpu = 0; cpu < s.cpuNum; cpu++) {
        if (s.fds[cpu] >= 0) {
            opened++;
            if (ioctl(s.fds[cpu], SIMPLE_PEBS_START, 0) < 0) {
                // pass
                failed++;
                failedCpus.push_back(cpu);
            }
        }
    }

    if (opened == 0) {
        pcerr::New(LIBPERF_ERR_LBR_DRIVER_INVALID, "Enable LBR driver failed: no CPUs opened.");
        return -1;
    }

    if (failed == opened) {
        pcerr::New(LIBPERF_ERR_LBR_DRIVER_INVALID, "Enable LBR driver failed: all opened CPUs failed to start.");
        return -1;
    }

    if (failed > 0) {
        std::string msg = "Enable LBR driver partial failure: failed=" + std::to_string(failed) +
                          " opened=" + std::to_string(opened) +". Failed CPUs:";
        for (int c : failedCpus) {
            msg += " " + std::to_string(c);
        }
        pcerr::SetWarn(LIBPERF_WARN_LBR_DRIVER_START_FAILED, msg);
    }

    s.running = true;
    return 0;
}

int PebsDriverManager::Disable(int pd)
{
    std::lock_guard<std::mutex> lk(driverMu);
    auto it = sess.find(pd);
    if (it == sess.end()) {
        return -1;
    }
    auto& s = it->second;
    if (!s.running) {
        return 0;
    }
    StopAll(it->second);
    return 0;
}

struct CpuChunk {
    int cpu;
    int len; 
};

static int TransferDriverToPmuData(PebsDriverSession& s, const CpuChunk& c, std::vector<PmuData>& outData,
                                   std::vector<PmuDataExt*>& extPool, int totalSamples, int& idx)
{
    if (c.cpu < 0 || c.cpu >= s.cpuNum) {
        return -1;
        }
    if (!s.maps[c.cpu] || s.maps[c.cpu] == MAP_FAILED) {
        return -1;
    }

    uint8_t* p   = reinterpret_cast<uint8_t*>(s.maps[c.cpu]);
    uint8_t* end = p + c.len;

    while (p + sizeof(simple_pebs_out_rec) <= end && idx < totalSamples) {
        auto* r = reinterpret_cast<simple_pebs_out_rec*>(p);
        if (r->size < sizeof(*r) || p + r->size > end) {
            break;
        }

        PmuData& d = outData[idx++];
        d.ts  = static_cast<int64_t>(r->tsc);
        d.pid = static_cast<pid_t>(r->tgid);
        d.tid = static_cast<int>(r->tid);
        d.cpu = static_cast<int>(c.cpu);
        d.evt = "cycles";
        d.ext = nullptr;

        int depth = static_cast<int>(r->lbr_depth);
        depth = std::min(depth, (int)SIMPLE_PEBS_MAX_LBR);
        if (depth <= 0) {
            p += r->size;
            continue;
        }

        int validIdx[SIMPLE_PEBS_MAX_LBR];
        int nr = 0;
        for (int i = 0; i < depth; i++) {
            uint64_t from = r->lbr_from[i];
            uint64_t to = r->lbr_to[i];
            if (!IsValidAddr(from) && !IsValidAddr(to)) {
                continue;
            }
            validIdx[nr++] = i;
        }

        if (nr > 0) {
            try {
                auto* ext = new PmuDataExt();
                auto* br  = new BranchSampleRecord[nr]();
                for (int k = 0; k < nr; k++) {
                    int i = validIdx[k];
                    uint64_t info = r->lbr_info[i];
                    br[k].fromAddr = r->lbr_from[i];
                    br[k].toAddr = r->lbr_to[i];
                    br[k].cycles = (unsigned long)(uint16_t)(info & 0xFFFF);
                    br[k].misPred = (uint8_t)((info >> 63) & 1);
                    br[k].predicted = (uint8_t)(br[k].misPred ? 0 : 1);
                }
                ext->nr = (unsigned long)nr;
                ext->branchRecords = br;
                d.ext = ext;
                extPool.push_back(ext);
            } catch (const std::bad_alloc&) {
                pcerr::New(LIBPERF_ERR_LBR_DRIVER_INVALID, "Read LBR driver failed: bad_alloc occurs.");
                return -1;
            }
        }
        p += r->size;
    }

    // prevent reading this CPU repeatedly
    if (s.fds[c.cpu] >= 0) {
        (void)ioctl(s.fds[c.cpu], SIMPLE_PEBS_RESET, 0);
    }
    return 0;
}

static inline void FreeExtPool(std::vector<PmuDataExt*>& pool)
{
    for (auto* ext : pool) {
        if (!ext) {
            continue;
        }
        delete[] ext->branchRecords;
        delete ext;
    }
    pool.clear();
}

int PebsDriverManager::Read(int pd, PmuData** out)
{
    if (!out) {
        return -1;
    }
    *out = nullptr;

    PebsDriverSession* sp = nullptr;
    {
        std::lock_guard<std::mutex> lk(driverMu);
        auto it = sess.find(pd);
        if (it == sess.end()) {
            return -1;
        }
        sp = &it->second;
    }
    PebsDriverSession& s = *sp;

    std::vector<CpuChunk> chunks;
    chunks.reserve(s.cpuNum);

    // if running: read CPUs with new data. Otherwise, read the offset for all CPUs to get samples
    if (s.running) {
        for (auto& p : s.pfds) {
            p.revents = 0;
        }
        (void)poll(s.pfds.data(), s.pfds.size(), 0);
    }

    for (int cpu = 0; cpu < s.cpuNum; cpu++) {
        int fd = s.fds[cpu];
        if (fd < 0) {
            continue;
        }
        if (s.running && !(s.pfds[cpu].revents & POLLIN)) {
            continue;
        }
        int len = 0;
        if (ioctl(fd, SIMPLE_PEBS_GET_OFFSET, &len) < 0) {
            (void)ioctl(fd, SIMPLE_PEBS_RESET, 0);
            continue;
        }
        if (len > 0) {
            chunks.push_back({cpu, len});
        }
    }

    if (chunks.empty()) {
        return 0;
    }

    int totalSamples = 0;
    for (auto& c : chunks) {
        if (!s.maps[c.cpu] || s.maps[c.cpu] == MAP_FAILED) {
            continue;
        }
        uint8_t* p   = (uint8_t*)s.maps[c.cpu];
        uint8_t* end = p + c.len;
        while (p + sizeof(simple_pebs_out_rec) <= end) {
            auto* r = (simple_pebs_out_rec*)p;
            if (r->size < sizeof(*r) || p + r->size > end) {
                break;
            }
            totalSamples++;
            p += r->size;
        }
    }
    if (totalSamples <= 0) {
        for (auto& c : chunks) {
            if (s.fds[c.cpu] >= 0) (void)ioctl(s.fds[c.cpu], SIMPLE_PEBS_RESET, 0);
        }
        return 0;
    }

    KUNPENG_PMU::EventData ed;
    ed.collectType = SAMPLING;
    ed.data.resize((size_t)totalSamples);
    ed.extPool.clear();
    ed.sampleIps.clear();
    ed.sampleIps.reserve((size_t)totalSamples);
    int idx = 0;
    for (auto& c : chunks) {
        int rc = TransferDriverToPmuData(
            s, c,
            ed.data,
            ed.extPool,
            totalSamples,
            idx
        );
        if (rc != 0) {
            FreeExtPool(ed.extPool);
            return -1;
        }
        if (idx >= totalSamples) {
            break;
        }
    }

    if (idx <= 0) {
        FreeExtPool(ed.extPool);
        return 0;
    }
    ed.data.resize((size_t)idx);
    PmuData* handle = KUNPENG_PMU::PmuList::GetInstance()->RegisterDriverHandle(std::move(ed));
    if (!handle) {
        return -1;
    }
    *out = handle;
    return idx;
}

void PebsDriverManager::Close(int pd)
{
    std::lock_guard<std::mutex> lk(driverMu);
    auto it = sess.find(pd);
    if (it == sess.end()) {
        return;
    }
    auto& s = it->second;

    StopAll(s);
    ResetAll(s);

    for (int cpu = 0; cpu < s.cpuNum; cpu++) {
        if (s.maps[cpu] && s.maps[cpu] != MAP_FAILED) {
            munmap(s.maps[cpu], s.driverMapSize);
            s.maps[cpu] = MAP_FAILED;
        }
        if (s.fds[cpu] >= 0) {
            close(s.fds[cpu]);
            s.fds[cpu] = -1;
        }
    }
    sess.erase(it);
}
