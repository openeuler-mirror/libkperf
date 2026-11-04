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
 * Description: LBR collection functions in Intel environment
 ******************************************************************************/
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <string>
#include <cstring>
#include <cstdint>
#include <memory>
#include <utility>
#include <sys/types.h>
#include <algorithm>

#include "pcerrc.h"
#include "pcerr.h"
#include "pmu_list.h"
#include "pmu_event.h"
#include "cpu_map.h"
#include "lbr_trace_backend.h"

static inline bool IsValidAddr(uint64_t a)
{
    if (a == 0 || a == ~0ull) {
        return false;
    }
    return ((a >> 63) == 0);
}

bool LbrDriverManager::ProbeDevice(int* out_size, int* out_cpus)
{
    if (!out_size || !out_cpus) {
        return false;
    }

    if (access("/dev/simple-lbr-trace", R_OK) != 0) {
        return false;
    }

    int fd = open("/dev/simple-lbr-trace", O_RDONLY);
    if (fd < 0) {
        return false;
    }

    int size = 0;
    int cpus = 0;
    if (ioctl(fd, SIMPLE_LBR_GET_SIZE, &size) < 0 || size <= 0) {
        close(fd);
        return false;
    }
    if (ioctl(fd, SIMPLE_LBR_GET_NR_CPUS, &cpus) < 0 || cpus <= 0) {
        close(fd);
        return false;
    }
    close(fd);
    *out_size = size;
    *out_cpus = cpus;
    return true;
}

bool LbrDriverManager::OpenDevice(LbrDriverSession& s, int targetPid)
{
    int fd = open("/dev/simple-lbr-trace", O_RDONLY);
    if (fd < 0) {
        return false;
    }

    if (ioctl(fd, SIMPLE_LBR_SET_PID, targetPid) < 0) {
        close(fd);
        return false;
    }

    if (ioctl(fd, SIMPLE_LBR_RESET, 0) < 0) {
        close(fd);
        return false;
    }
    // one ring buffer for each cpu
    for (int cpu = 0; cpu < s.cpuNum; cpu++) {
        off_t off = static_cast<off_t>(cpu) * static_cast<off_t>(s.driverMapSize);
        void* addr = mmap(nullptr, s.driverMapSize, PROT_READ, MAP_SHARED, fd, off);
        if (addr == MAP_FAILED) {
            for (int j = 0; j < cpu; j++) {
                if (s.maps[j] && s.maps[j] != MAP_FAILED) {
                    munmap(s.maps[j], s.driverMapSize);
                    s.maps[j] = MAP_FAILED;
                }
            }
            close(fd);
            return false;
        }
        s.maps[cpu] = addr;
    }

    s.fd = fd;
    s.pfd.fd = fd;
    s.pfd.events = POLLIN;
    s.pfd.revents = 0;
    s.targetPid = targetPid;
    return true;
}

void LbrDriverManager::Stop(LbrDriverSession& s)
{
    if (s.fd >= 0) {
        (void)ioctl(s.fd, SIMPLE_LBR_STOP, 0);
    }
    s.running = false;
}

int LbrDriverManager::Open(struct PmuAttr *attr)
{
    int size = 0;
    int cpus = 0;
    if (attr == nullptr || attr->pidList == nullptr || attr->numPid != 1) {
        pcerr::New(LIBPERF_ERR_LBR_DRIVER_INVALID, "Open LBR driver failed: one target pid is required.");
        return -1;
    }
    int targetPid = attr->pidList[0];

    if (!ProbeDevice(&size, &cpus)) {
        pcerr::New(LIBPERF_ERR_LBR_DRIVER_INVALID, "Probe LBR driver failed.");
        return -1;
    }

    auto s = std::make_shared<LbrDriverSession>();
    s->driverMapSize = size;
    s->cpuNum = cpus;
    s->maps.assign(s->cpuNum, MAP_FAILED);
    s->consumedSeq.assign(s->cpuNum, 0);
    s->lastTailSeq.assign(s->cpuNum, 0);
    s->lastOverwritten.assign(s->cpuNum, 0);
    s->lastFailures.assign(s->cpuNum, 0);
    s->running = false;
    s->targetPid = targetPid;

    if (!OpenDevice(*s, targetPid)) {
        pcerr::New(LIBPERF_ERR_LBR_DRIVER_INVALID, "Open LBR driver device failed.");
        return -1;
    }

    int pd = nextDriverPd.fetch_add(1);
    sess.emplace(pd, s);
    return pd;
}

std::shared_ptr<LbrDriverSession> LbrDriverManager::GetSession(int pd)
{
    auto it = sess.find(pd);
    if (it == sess.end()) {
        return nullptr;
    }
    return it->second;
}

int LbrDriverManager::Enable(int pd)
{
    auto s = GetSession(pd);
    if (!s) {
        return -1;
    }
    if (s->fd < 0) {
        return -1;
    }

    if (ioctl(s->fd, SIMPLE_LBR_RESET, 0) < 0) {
        pcerr::New(LIBPERF_ERR_LBR_DRIVER_INVALID, "Enable LBR driver failed: RESET failed.");
        return -1;
    }

    std::fill(s->consumedSeq.begin(), s->consumedSeq.end(), 0);
    std::fill(s->lastTailSeq.begin(), s->lastTailSeq.end(), 0);
    std::fill(s->lastOverwritten.begin(), s->lastOverwritten.end(), 0);
    std::fill(s->lastFailures.begin(), s->lastFailures.end(), 0);

    if (ioctl(s->fd, SIMPLE_LBR_START, 0) < 0) {
        pcerr::New(LIBPERF_ERR_LBR_DRIVER_INVALID, "Enable LBR driver failed: START failed.");
        return -1;
    }

    s->running = true;
    return 0;
}

int LbrDriverManager::Disable(int pd)
{
    auto s = GetSession(pd);
    if (!s) {
        return -1;
    }
    if (!s->running) {
        return 0;
    }

    Stop(*s);
    return 0;
}


static int TransferLbrRingToPmuData(LbrDriverSession& s, const CpuChunk& c, std::vector<PmuData>& outData,
                                    std::vector<KUNPENG_PMU::PerfSampleIps>& sampleIps,
                                    std::vector<PmuDataExt*>& extPool,
                                    int totalSamples, int& idx)
{
    if (c.cpu < 0 || c.cpu >= s.cpuNum) {
        return -1;
    }
    if (!s.maps[c.cpu] || s.maps[c.cpu] == MAP_FAILED) {
        return -1;
    }

    auto* recs = reinterpret_cast<simple_lbr_out_rec*>(s.maps[c.cpu]);
    for (uint64_t seq = c.beginSeq; seq < c.endSeq && idx < totalSamples; seq++) {
        auto* r = &recs[seq & (SIMPLE_LBR_RING_SLOTS - 1)];
        if (r->size != sizeof(*r)) {
            continue;
        }

        PmuData& d = outData[idx++];
        d.ts  = static_cast<int64_t>(r->tsc);
        d.pid = static_cast<pid_t>(r->tgid);
        d.tid = static_cast<int>(r->tid);
        d.cpu = static_cast<int>(r->cpu);
        d.evt = "lbr_trace";
        d.ext = nullptr;
        d.period = 1;
        sampleIps.emplace_back();
        sampleIps.back().ips.push_back((unsigned long)r->ip);

        int depth = static_cast<int>(r->lbr_depth);
        depth = std::min(depth, (int)SIMPLE_LBR_MAX_LBR);
        if (depth <= 0) {
            continue;
        }

        int validIdx[SIMPLE_LBR_MAX_LBR];
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
                std::unique_ptr<PmuDataExt> ext(new PmuDataExt());
                std::unique_ptr<BranchSampleRecord[]> br(new BranchSampleRecord[nr]());
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
                ext->branchRecords = br.get();
                extPool.push_back(ext.get());
                d.ext = ext.release();
                (void)br.release();
            } catch (const std::bad_alloc&) {
                pcerr::New(LIBPERF_ERR_LBR_DRIVER_INVALID, "Read LBR driver failed: bad_alloc occurs.");
                return -1;
            }
        }
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

int LbrDriverManager::Read(int pd, PmuData** out)
{
    if (!out) {
        return -1;
    }
    *out = nullptr;

    auto s = GetSession(pd);
    if (!s) {
        return -1;
    }
    if (s->fd < 0) {
        return -1;
    }

    std::vector<CpuChunk> chunks;
    chunks.reserve(s->cpuNum);

    if (s->running) {
        s->pfd.revents = 0;
        (void)poll(&s->pfd, 1, 0);
        if (!(s->pfd.revents & POLLIN)) {
            return 0;
        }
    }

    for (int cpu = 0; cpu < s->cpuNum; cpu++) {
        simple_lbr_cpu_stats st{};
        st.cpu = static_cast<uint32_t>(cpu);
        if (ioctl(s->fd, SIMPLE_LBR_GET_CPU_STATUS, &st) < 0) {
            continue;
        }

        if (st.tail_seq != s->lastTailSeq[cpu]) {
            s->lastTailSeq[cpu] = st.tail_seq;
        }

        if (st.overwrite_records != s->lastOverwritten[cpu] || st.enable_failures != s->lastFailures[cpu]) {
            std::string msg = "LBR cpu=" + std::to_string(cpu) +" overwritten=" + std::to_string(st.overwrite_records) +
                              " enable_failures=" + std::to_string(st.enable_failures);
            pcerr::SetWarn(LIBPERF_WARN_LBR_PARTIAL_FAILED, msg);
            s->lastOverwritten[cpu] = st.overwrite_records;
            s->lastFailures[cpu] = st.enable_failures;
        }

        uint64_t begin = s->consumedSeq[cpu];
        uint64_t oldestValid = st.tail_seq;
        uint64_t end = st.head_seq;

        // If the user mode is later than oldest valid, jump to oldest valid
        if (begin < oldestValid) {
            begin = oldestValid;
            s->consumedSeq[cpu] = oldestValid;
        }
        if (end > begin) {
            chunks.emplace_back(cpu, begin, end);
        }
    }

    if (chunks.empty()) {
        return 0;
    }

    int totalSamples = 0;
    for (const auto& c : chunks) {
        totalSamples += (int)c.endSeq - c.beginSeq;
    }
    if (totalSamples <= 0) {
        return 0;
    }

    KUNPENG_PMU::EventData ed;
    ed.collectType = SAMPLING;
    ed.data.resize(static_cast<size_t>(totalSamples));
    ed.extPool.clear();
    int idx = 0;
    for (const auto& c : chunks) {
        int rc = TransferLbrRingToPmuData(
            *s, c,
            ed.data,
            ed.sampleIps,
            ed.extPool,
            totalSamples,
            idx
        );
        if (rc != 0) {
            FreeExtPool(ed.extPool);
            return -1;
        }
        s->consumedSeq[c.cpu] = c.endSeq;
        simple_lbr_tail_advance adv{};
        adv.cpu = (uint32_t)c.cpu;
        adv.tail_seq = c.endSeq;
        (void)ioctl(s->fd, SIMPLE_LBR_ADVANCE_TAIL, &adv);
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

void LbrDriverManager::Close(int pd)
{
    auto it = sess.find(pd);
    if (it == sess.end()) {
        return;
    }
    auto s = it->second;
    sess.erase(it);
    Stop(*s);
    if (s->fd >= 0) {
        (void)ioctl(s->fd, SIMPLE_LBR_RESET, 0);
    }
    for (int cpu = 0; cpu < s->cpuNum; cpu++) {
        if (s->maps[cpu] && s->maps[cpu] != MAP_FAILED) {
            munmap(s->maps[cpu], s->driverMapSize);
            s->maps[cpu] = MAP_FAILED;
        }
    }
    if (s->fd >= 0) {
        close(s->fd);
        s->fd = -1;
    }
}