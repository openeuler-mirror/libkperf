/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
 * gala-gopher licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: Mr.Jin
 * Create: 2024-04-03
 * Description: implements functionalities for reading and processing System Performance Events (SPE)
 * data in the KUNPENG_PMU namespace
 ******************************************************************************/
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include "pmu_event.h"
#include "arm_spe_decoder.h"
#include "process_map.h"
#include "log.h"
#include "pcerr.h"
#include "evt.h"
#include "spe.h"

using namespace std;
using namespace KUNPENG_PMU;

constexpr unsigned SPE_RECORD_MAX = 100000;
constexpr unsigned BUFF_SIZE = 64;
/* Should align to 2^n size in pages */
constexpr unsigned RING_BUF_SIZE = 64 * 1024;
constexpr unsigned AUX_BUF_SIZE = 256 * 1024;


struct AuxContext {
    struct ContextSwitchData *dummyData;
    int *dummyIdx;
    int cpu;
    size_t auxOffset;
    size_t auxSize;
};

static int OpenSpeEvent(PmuEvt *pmuAttr, int cpu)
{
    struct perf_event_attr attr = {0};

    attr.size = sizeof(attr);
    attr.type = pmuAttr->type;
    attr.config = pmuAttr->config; /* pa_enable | load_filter | store_filter | ts_enable */
    attr.config1 = pmuAttr->config1;        /* event_filter */
    attr.config2 = pmuAttr->config2;       /* min_latency */
    attr.exclude_guest = 1;
    attr.disabled = 1;
    attr.freq = pmuAttr->useFreq;
    attr.sample_period = pmuAttr->period;
    attr.sample_type = PERF_SAMPLE_TID;
    attr.sample_id_all = 1;
    attr.read_format = PERF_FORMAT_ID;

    return PerfEventOpen(&attr, -1, cpu, -1, 0);
}

static int OpenDummyEvent(int cpu)
{
    struct perf_event_attr attr = {0};

    attr.size = sizeof(attr);
    attr.type = PERF_TYPE_SOFTWARE;
    attr.config = PERF_COUNT_SW_DUMMY;
    attr.exclude_kernel = 1;
    attr.disabled = 1;
    attr.sample_period = 1;
    attr.sample_type = PERF_SAMPLE_TIME;
    attr.sample_id_all = 1;
    attr.read_format = PERF_FORMAT_ID;
    attr.context_switch = 1;
    attr.mmap = 1;
    attr.task = 1;
    attr.inherit = 1;
    attr.exclude_guest = 1;

    return PerfEventOpen(&attr, -1, cpu, -1, 0);
}

static int PerfReadTscConversion(const struct perf_event_mmap_page *pc, struct PerfTscConversion *tc)
{
    uint32_t seq;
    int i = 0;

    while (1) {
        seq = pc->lock;
        RMB();
        tc->timeMult = pc->time_mult;
        tc->timeShift = pc->time_shift;
        tc->timeZero = pc->time_zero;
        tc->capUserTimeZero = pc->cap_user_time_zero;
        RMB();

        if (pc->lock == seq && !(seq & 1)) {
            break;
        }
        if (++i > 10000) {
            return LIBPERF_ERR_KERNEL_NOT_SUPPORT;
        }
    }

    if (!tc->capUserTimeZero) {
        return LIBPERF_ERR_KERNEL_NOT_SUPPORT;
    }

    return SUCCESS;
}

static uint64_t TscToPerfTime(uint64_t cyc, struct PerfTscConversion *tc)
{
    uint64_t quot = cyc >> tc->timeShift;
    uint64_t rem = cyc & ((static_cast<uint64_t>(1) << tc->timeShift) - 1);

    return tc->timeZero + quot * tc->timeMult + ((rem * tc->timeMult) >> tc->timeShift);
}

static void CoreSpeClose(struct SpeCoreContext *ctx, struct SpeContext *speCtx)
{
    if (ctx->speFd > 0) {
        close(ctx->speFd);
    }

    if (ctx->dummyFd > 0) {
        close(ctx->dummyFd);
    }

    if (ctx->speMpage && ctx->speMpage != MAP_FAILED) {
        munmap(ctx->speMpage, speCtx->speMmapSize);
    }

    if (ctx->auxMpage && ctx->auxMpage != MAP_FAILED) {
        munmap(ctx->auxMpage, speCtx->auxMmapSize);
    }

    if (ctx->dummyMpage && ctx->dummyMpage != MAP_FAILED) {
        munmap(ctx->dummyMpage, speCtx->dummyMmapSize);
    }

    memset(ctx, 0, sizeof(*ctx));
}

static int CoreSpeOpenFailed(struct SpeCoreContext **ctx, struct SpeContext *speCtx)
{
    CoreSpeClose(*ctx, speCtx);
    return LIBPERF_ERR_SPE_UNAVAIL;
}

static int CoreSpeOpen(struct SpeCoreContext **ctx, struct SpeContext *speCtx, PmuEvt *attr, int cpu)
{
    int ret = -1;
    struct perf_event_mmap_page *mp = nullptr;

    (*ctx)->cpu = cpu;
    (*ctx)->speFd = OpenSpeEvent(attr, cpu);
    if ((*ctx)->speFd < 0) {
        auto err = MapErrno(errno);
        ERR_PRINT("failed to open spe\n");
        CoreSpeClose(*ctx, speCtx);
        return err;
    }
    DBG_PRINT("perf_event_open, cpu: %d fd: %d\n", cpu, (*ctx)->speFd);

    (*ctx)->speMpage = mmap(nullptr, speCtx->speMmapSize, PROT_READ | PROT_WRITE, MAP_SHARED, (*ctx)->speFd, 0);
    if ((*ctx)->speMpage == MAP_FAILED) {
        ERR_PRINT("failed to mmap for spe event\n");
        return CoreSpeOpenFailed(ctx, speCtx);
    }

    mp = static_cast<perf_event_mmap_page*>((*ctx)->speMpage);
    mp->aux_offset = speCtx->speMmapSize;
    mp->aux_size = speCtx->auxMmapSize;

    (*ctx)->auxMpage = mmap(nullptr, mp->aux_size, PROT_READ | PROT_WRITE, MAP_SHARED, (*ctx)->speFd, mp->aux_offset);
    if ((*ctx)->auxMpage == MAP_FAILED) {
        ERR_PRINT("failed to mmap for aux event\n");
        return CoreSpeOpenFailed(ctx, speCtx);
    }

    (*ctx)->dummyFd = OpenDummyEvent(cpu);
    if ((*ctx)->dummyFd < 0) {
        ERR_PRINT("failed to open dummy event fd\n");
        return CoreSpeOpenFailed(ctx, speCtx);
    }

    (*ctx)->dummyMpage = mmap(nullptr, speCtx->dummyMmapSize, PROT_READ | PROT_WRITE, MAP_SHARED, (*ctx)->dummyFd, 0);
    if ((*ctx)->dummyMpage == MAP_FAILED) {
        ERR_PRINT("failed to mmap for dummy event\n");
        return CoreSpeOpenFailed(ctx, speCtx);
    }

    return SUCCESS;
}

int SpeOpen(PmuEvt *attr, int cpu, SpeContext *ctx)
{
    int pageSize = sysconf(_SC_PAGESIZE);

    if (attr->type == -1) {
        free(ctx);
        return LIBPERF_ERR_SPE_UNAVAIL;
    }

    ctx->cpuNum = 1;
    ctx->pageSize = pageSize;

    /* Should align to 2^n size in pages */
    ctx->speMmapSize = RING_BUF_SIZE + static_cast<unsigned>(pageSize);
    ctx->auxMmapSize = AUX_BUF_SIZE;
    ctx->dummyMmapSize = RING_BUF_SIZE + pageSize;

    ctx->coreCtxes = (struct SpeCoreContext *)malloc(sizeof(struct SpeCoreContext));
    ctx->coreCtxes->mask = ctx->auxMmapSize - 1;
    ctx->coreCtxes->prev = 0;
    if (!ctx->coreCtxes) {
        free(ctx);
        return COMMON_ERR_NOMEM;
    }

    auto err = CoreSpeOpen(&ctx->coreCtxes, ctx, attr, cpu);
    if (err != 0) {
        free(ctx->coreCtxes);
        free(ctx);
        return err;
    }
    return SUCCESS;
}

static int CoreSpeEnable(struct SpeCoreContext *ctx)
{
    if (ctx->dummyFd <= 0 || ctx->speFd <= 0) {
        return -1;
    }

    ioctl(ctx->dummyFd, PERF_EVENT_IOC_ENABLE, 0);
    ioctl(ctx->speFd, PERF_EVENT_IOC_ENABLE, 0);
    return 0;
}

int SpeEnable(struct SpeContext *ctx)
{
    for (int i = 0; i < ctx->cpuNum; i++) {
        CoreSpeEnable(&ctx->coreCtxes[i]);
    }

    return 0;
}

static int CoreSpeDisable(struct SpeCoreContext *ctx)
{
    if (ctx->dummyFd <= 0 || ctx->speFd <= 0) {
        return -1;
    }

    ioctl(ctx->speFd, PERF_EVENT_IOC_DISABLE, 0);
    ioctl(ctx->dummyFd, PERF_EVENT_IOC_DISABLE, 0);
    return 0;
}

void SpeDisable(struct SpeContext *ctx)
{
    for (int i = 0; i < ctx->cpuNum; i++) {
        CoreSpeDisable(&ctx->coreCtxes[i]);
    }

    return;
}

void SpeClose(struct SpeContext *ctx)
{
    for (int i = 0; i < ctx->cpuNum; i++) {
        CoreSpeClose(&ctx->coreCtxes[i], ctx);
    }

    free(ctx->coreCtxes);
    free(ctx);
    return;
}

void Spe::UpdateProcMap(__u32 ppid, __u32 pid)
{
    auto findParent = procMap.find(ppid);
    if (findParent != procMap.end()) {
        auto procTopo = GetProcTopology(pid);
        if (procTopo != nullptr) {
            if (procMap.find(pid) == procMap.end()) {
                DBG_PRINT("Add to proc map: %d\n", pid);
                procMap[pid] = shared_ptr<ProcTopology>(procTopo, FreeProcTopo);
            } else {
                FreeProcTopo(procTopo);
            }
        }
    }
}

static void ParseContextSwitch(PerfEventSampleContextSwitch *contextSample, ContextSwitchData *data, uint64_t *num,
                               ContextSwitchData *lastSwitchOut)
{
    if (contextSample->header.misc == 0 && contextSample->time < 1e18) {
        // Context switch for Switch-In, <contextSample->nextPrevPid> is active pid before <contextSample->time>.
        // Use switch-in data for spe timestamp analysis, because switch-out data miss some context switch info.
        data[*num].nextPrevPid = contextSample->nextPrevPid;
        data[*num].nextPrevTid = contextSample->nextPrevTid;
        data[*num].time = contextSample->time;
        (*num)++;
    }
    if (contextSample->header.misc == PERF_RECORD_MISC_SWITCH_OUT && contextSample->time < 1e18) {
        // keep track of the last switch-out data, which will be used for last time slice.
        lastSwitchOut->nextPrevPid = contextSample->nextPrevPid;
        lastSwitchOut->nextPrevTid = contextSample->nextPrevTid;
        lastSwitchOut->time = contextSample->time;
        (*num)++;
    }
}

void Spe::CoreDummyData(struct SpeCoreContext *context, struct ContextSwitchData *data, int size, int pageSize)
{
    uint64_t maxNum = size / sizeof(struct ContextSwitchData);
    uint64_t num = 1;
    struct perf_event_mmap_page *mpage = (struct perf_event_mmap_page *)context->dummyMpage;
    uint8_t *ringBuf = (uint8_t *)(mpage) + pageSize;
    uint64_t dataHead = mpage->data_head;
    uint64_t dataTail = mpage->data_tail;

    RMB();
    ContextSwitchData lastSwitchOut;
    while ((dataTail < dataHead) && (num < maxNum)) {
        uint64_t off = dataTail % mpage->data_size;
        struct perf_event_header *header = (struct perf_event_header *)(ringBuf + off);

        if (header->type == PERF_RECORD_MMAP) {
            struct PerfRecordMmap *sample = (struct PerfRecordMmap *)header;
            SymResolverUpdateModule(sample->tid, sample->filename, sample->addr);
            dataTail += header->size;
            continue;
        }
        if (header->type == PERF_RECORD_FORK) {
            struct PerfRecordFork *sample = (struct PerfRecordFork *)header;
            DBG_PRINT("Fork pid: %d tid: %d\n", sample->pid, sample->tid);
            UpdateProcMap(sample->pid, sample->tid);
            dataTail += header->size;
            continue;
        }

        if ((off + header->size) > mpage->data_size || header->type != PERF_RECORD_SWITCH_CPU_WIDE) {
            /* skip the wrap record or invalid record */
            dataTail += header->size;
            continue;
        }

        struct PerfEventSampleContextSwitch *contextSample = (struct PerfEventSampleContextSwitch *)header;
        ParseContextSwitch(contextSample, data, &num, &lastSwitchOut);
        dataTail += header->size;
    }

    // Put last switch out to the end of dummy data.
    data[num].nextPrevPid = lastSwitchOut.nextPrevPid;
    data[num].nextPrevTid = lastSwitchOut.nextPrevTid;
    data[num].time = lastSwitchOut.time;
    ++num;
    data[0].num = num;
    data[0].nextPrevPid = -1;
    data[0].nextPrevTid = -1;

    mpage->data_tail = mpage->data_head;
    MB();
}

static void SetTidByTimestamp(struct ContextSwitchData *dummyData, int *dummyIdx, struct SpeRecord *buf,
                              struct SpeRecord *bufEnd, int cpu, struct PerfTscConversion *tc)
{
    for (struct SpeRecord *start = buf; start < bufEnd; start++) {
        uint64_t recordTime = TscToPerfTime(start->timestamp, tc);

        start->cpu = cpu;
        start->timestamp = recordTime;

        if (*dummyIdx >= dummyData[0].num - 1) {
            // Now, all spe records locate after the last switch-in data.
            // We have to use switch-out data to get pid of the last time slice.
            start->pid = dummyData[dummyData[0].num - 1].nextPrevPid;
            start->tid = dummyData[dummyData[0].num - 1].nextPrevTid;
            continue;
        }

        for (; *dummyIdx < dummyData[0].num - 1; (*dummyIdx)++) {
            if (dummyData[*dummyIdx].time > recordTime) {
                // <recordTime> is located between dummyData[*dummyIdx-1].time and dummyData[*dummyIdx].time.
                // Then pid is the prev pid of dummyData[*dummyIdx].
                start->pid = dummyData[*dummyIdx].nextPrevPid;
                start->tid = dummyData[*dummyIdx].nextPrevTid;
                break;
            }
        }
    }

    return;
}

static struct SpeRecord *CoreAuxData(struct SpeCoreContext *ctx, AuxContext *auxCtx,
                                     struct SpeRecord *buf, int *remainSize)
{
    struct perf_event_mmap_page *mpage = (struct perf_event_mmap_page *)ctx->speMpage;
    uint8_t *auxBuf = static_cast<uint8_t *>(ctx->auxMpage);
    uint8_t *auxStart = auxBuf + auxCtx->auxOffset % mpage->aux_size;
    uint8_t *auxEnd = auxStart + auxCtx->auxSize;
    SpeRecord *bufEnd = SpeGetRecord(auxStart, auxEnd, buf, remainSize);

    struct PerfTscConversion tc;
    auto err = PerfReadTscConversion(mpage, &tc);
    if (err != SUCCESS) {
        pcerr::New(err);
        return nullptr;
    }
    SetTidByTimestamp(auxCtx->dummyData, auxCtx->dummyIdx, buf, bufEnd, auxCtx->cpu, &tc);

    return bufEnd;
}

static size_t ComputeAuxSize(size_t auxMapLen, size_t headOff, size_t oldOff, int pageSize)
{
    // Compute current aux buffer size by current offset and previous offset.
    size_t size = 0;
    if (headOff > oldOff) {
        // Normal case, just diff of two offsets.
        size = headOff - oldOff;
    } else {
        // Wraparound, size equals sum of two segment.
        size = auxMapLen - (oldOff - headOff);
    }
    return size;
}

static struct SpeRecord *CoreSpeData(struct SpeCoreContext *ctx, struct ContextSwitchData *dummyData,
                                     struct SpeRecord *buf, int *remainSize, int pageSize, int cpu)
{
    int dummyIdx = 1;
    struct perf_event_mmap_page *mpage = (struct perf_event_mmap_page *)ctx->speMpage;
    RMB();
    __u64 old = ctx->prev;
    __u64 head = ReadOnce(&mpage->aux_head);
    if (old == head) {
        return buf;
    }
    size_t headOff = head & ctx->mask;
    size_t oldOff = old & ctx->mask;
    size_t size = ComputeAuxSize(mpage->aux_size, headOff, oldOff, pageSize);

    size_t auxOffset = 0;
    struct SpeRecord *bufEnd = nullptr;
    AuxContext auxCtx = {.dummyData = dummyData,
            .dummyIdx = &dummyIdx,
            .cpu = cpu};
    if (size > headOff) {
        // Wraparound, read two data segments.
        // Read the tail segment.
        auxCtx.auxSize = size - headOff;
        auxCtx.auxOffset = mpage->aux_size - auxCtx.auxSize;
        buf = CoreAuxData(ctx, &auxCtx, buf, remainSize);
        // Read the head segment.
        auxCtx.auxOffset = 0;
        auxCtx.auxSize = headOff;
        buf = CoreAuxData(ctx, &auxCtx, buf, remainSize);
    } else {
        auxCtx.auxOffset = oldOff;
        auxCtx.auxSize = size;
        buf = CoreAuxData(ctx, &auxCtx, buf, remainSize);
    }
    ctx->prev = head;

    mpage->data_tail = mpage->data_head;
    mpage->aux_tail = mpage->aux_head;
    MB();

    return buf;
}

/*
 * For the initial implementation, caller should allocate a big enough buffer to
 * contain all of spe records. It's not pretty frankly, will be improved later.
 */
int Spe::SpeReadData(struct SpeContext *context, struct SpeRecord *buf, int size)
{
    int remainSize = size;
    int dummySize = context->dummyMmapSize;
    CoreDummyData(context->coreCtxes, dummyData, dummySize, context->pageSize);
    buf = CoreSpeData(context->coreCtxes, dummyData, buf, &remainSize, context->pageSize, cpu);
    return size - remainSize;
}

int Spe::Open(PmuEvt *attr)
{
    if (status == NONE) {
        ctx = (struct SpeContext *)malloc(sizeof(struct SpeContext));
        if (!ctx) {
            return COMMON_ERR_NOMEM;
        }
        auto err = SpeOpen(attr, cpu, ctx);
        if (err != SUCCESS) {
            return err;
        }
        status |= OPENED;
        this->dummyFd = this->ctx->coreCtxes->dummyFd;
        this->fd = this->ctx->coreCtxes->speFd;

        if (records == nullptr) {
            records = new SpeRecord[SPE_RECORD_MAX];
        }
        if (dummyData == nullptr) {
            dummyData = new ContextSwitchData[ctx->dummyMmapSize];
        }
    }

    return SUCCESS;
}
bool Spe::Enable(bool clearPrevRecords)
{
    if (clearPrevRecords) {
        pidRecords.clear();
    }

    if (!(status & OPENED)) {
        return false;
    }
    if (status & ENABLED) {
        return true;
    }
    SpeEnable(ctx);
    status &= ~DISABLED;
    status &= ~READ;
    status |= ENABLED;
    return true;
}
bool Spe::Disable()
{
    if (!(status & OPENED)) {
        return false;
    }
    if (status & DISABLED) {
        return true;
    }
    SpeDisable(ctx);
    status &= ~ENABLED;
    status |= DISABLED;
    return true;
}
bool Spe::Close()
{
    if (status == NONE) {
        return true;
    }
    SpeClose(ctx);
    if (records != nullptr) {
        delete[] records;
        records = nullptr;
    }
    if (dummyData != nullptr) {
        delete[] dummyData;
        dummyData = nullptr;
    }
    status = NONE;
    return true;
}

int Spe::Read()
{
    if (!(status & OPENED)) {
        return UNKNOWN_ERROR;
    }
    if (status & READ) {
        return SUCCESS;
    }
    int numRecord = SpeReadData(this->ctx, records, SPE_RECORD_MAX);
    for (int i = 0; i < numRecord; i++) {
        SpeRecord *rec = &records[i];
        pidRecords[rec->tid].push_back(rec);
    }
    status |= READ;
    if (Perrorno() == LIBPERF_ERR_KERNEL_NOT_SUPPORT) {
        return Perrorno();
    }
    return SUCCESS;
}

bool Spe::HaveRead()
{
    return status & READ;
}

const std::vector<SpeRecord *> Spe::GetPidRecords(const pid_t &pid) const
{
    auto findRecords = pidRecords.find(pid);
    if (findRecords == pidRecords.end()) {
        return {};
    }
    return findRecords->second;
}
