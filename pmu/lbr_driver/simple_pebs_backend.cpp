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
    int fail = 0;
    ResetAll(s);
    for (int cpu = 0; cpu < s.cpuNum; cpu++) {
        if (s.fds[cpu] >= 0) {
            if (ioctl(s.fds[cpu], SIMPLE_PEBS_START, 0) < 0) {
                // pass
                fail++;
            }
        }
    }
    if (fail == s.cpuNum) {
        return -1;
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
    StopAll(it->second);
    return 0;
}

struct CpuChunk {
    int cpu;
    int len; 
};

static int TransferDriverToPmuData(PebsDriverSession& s, const CpuChunk& c, PmuData* data,
                                   int totalSamples, int& idx)
{
    if (!data || totalSamples <= 0) return -1;
    if (c.cpu < 0 || c.cpu >= s.cpuNum) return -1;
    if (!s.maps[c.cpu] || s.maps[c.cpu] == MAP_FAILED) return -1;

    uint8_t* p   = reinterpret_cast<uint8_t*>(s.maps[c.cpu]);
    uint8_t* end = p + c.len;

    while (p + sizeof(simple_pebs_out_rec) <= end && idx < totalSamples) {
        auto* r = reinterpret_cast<simple_pebs_out_rec*>(p);
        if (r->size < sizeof(*r) || p + r->size > end) {
            break;
        }

        PmuData& d = data[idx++];
        d.ts  = static_cast<int64_t>(r->tsc);
        d.pid = static_cast<pid_t>(r->tgid);
        d.tid = static_cast<int>(r->tid);
        d.cpu = static_cast<int>(c.cpu);
        d.ext = nullptr;

        int depth = static_cast<int>(r->lbr_depth);
        depth = std::min(depth, (int)SIMPLE_PEBS_MAX_LBR);
        if (depth <= 0) {
            p += r->size;
            continue;
        }

        PmuDataExt* ext = nullptr;
        BranchSampleRecord* br = nullptr;
        try {
            ext = new PmuDataExt();
            br  = new BranchSampleRecord[depth]();

            int k = 0;
            for (int i = 0; i < depth; i++) {
                uint64_t from = r->lbr_from[i];
                uint64_t to   = r->lbr_to[i];
                uint64_t info = r->lbr_info[i];
                if (!IsValidAddr(from) || !IsValidAddr(to)) {
                    continue;
                }
                br[k].fromAddr  = from;
                br[k].toAddr    = to;
                br[k].cycles    = static_cast<unsigned long>(static_cast<uint16_t>(info & 0xFFFF));
                br[k].misPred   = static_cast<uint8_t>((info >> 63) & 1);
                br[k].predicted = static_cast<uint8_t>(br[k].misPred ? 0 : 1);
                k++;
            }

            if (k == 0) {
                delete[] br;
                delete ext;
            } else {
                ext->nr = static_cast<unsigned long>(k);
                ext->branchRecords = br;
                d.ext = ext;
                ext = nullptr;
                br = nullptr;
            }
        } catch (const std::bad_alloc&) {
            delete[] br;
            delete ext;
        }
        p += r->size;
    }

    // prevent reading this CPU repeatedly
    if (s.fds[c.cpu] >= 0) {
        (void)ioctl(s.fds[c.cpu], SIMPLE_PEBS_RESET, 0);
    }
    return 0;
}

int PebsDriverManager::Read(int pd, PmuData** out)
{
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
        if (fd < 0) continue;
        if (s.running && !(s.pfds[cpu].revents & POLLIN)) {
            continue;
        }

        int len = 0;
        if (ioctl(fd, SIMPLE_PEBS_GET_OFFSET, &len) < 0) {
            ioctl(fd, SIMPLE_PEBS_RESET, 0);
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
        uint8_t* p = (uint8_t*)s.maps[c.cpu];
        uint8_t* end = p + c.len;
        while (p + sizeof(simple_pebs_out_rec) <= end) {
            auto* r = (simple_pebs_out_rec*)p;
            if (r->size < sizeof(*r) || p + r->size > end) break;
            totalSamples++;
            p += r->size;
        }
    }
    if (totalSamples <= 0) {
        for (auto& c : chunks) ioctl(s.fds[c.cpu], SIMPLE_PEBS_RESET, 0);
        return 0;
    }

    PmuData* data = (PmuData*)calloc((size_t)totalSamples, sizeof(PmuData));
    if (!data) {
        return -1;
    }

    int idx = 0;
    for (auto& c : chunks) {
        TransferDriverToPmuData(s, c, data, totalSamples, idx);
        if (idx >= totalSamples) {
            break;
        }
    }
    *out = data;
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
