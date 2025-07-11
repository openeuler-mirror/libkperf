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
 * Author: Mr.Gan
 * Create: 2025-07-09
 * Description: Write PmuData list to file like perf.data.
 ******************************************************************************/
#include "pmu.h"
#include "pcerrc.h"
#include "pcerr.h"
#include "symbol.h"
#include "pmu_event.h"
#include "pmu_list.h"
#include "process_map.h"
#include "pfm.h"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <cstdio>
#include <map>
#include <fstream>
#include <linux/perf_event.h>
#include <sys/mman.h>

using namespace std;
using namespace KUNPENG_PMU;
using namespace pcerr;

#define PERF_ALIGN(x, a)        __PERF_ALIGN_MASK(x, (typeof(x))(a)-1)
#define __PERF_ALIGN_MASK(x, mask)      (((x)+(mask))&~(mask))
constexpr static size_t BUILD_ID_LEN = 20;

// These structs mostly come from linux tools/perf/util/header.h
struct PerfFileSection {
    uint64_t offset; /* offset from start of file */
    uint64_t size;   /* size of the section */
};

struct PerfFileAttr {
    struct perf_event_attr attr;
    struct PerfFileSection ids;
};

struct PerfFileHeader {
    char magic[8];      /* PERFILE2 */
    uint64_t size;      /* size of the header */
    uint64_t attrSize; /* size of an attribute in attrs */
    struct PerfFileSection attrs;
    struct PerfFileSection data;
    struct PerfFileSection event_types;
    uint64_t addsFeatures[4];
};

struct PerfBuildId {
    struct perf_event_header header;
    pid_t pid;
    union {
        uint8_t buildId[24];
        struct {
            uint8_t data[BUILD_ID_LEN];
            uint8_t size;
            uint8_t reserved1__;
            uint16_t reserved2__;
        };
    };
    char filename[];
};

// Simplified perf sample struct, only includes essential fields.
struct PerfSample {
    perf_event_header header;
    __u64 sampleid;
    __u64 ip;
    __u32 pid, tid;
    __u64 time;
    __u64 id;
    __u32 cpu;
    __u64 period;
    __u64 bnr;
    perf_branch_entry lbr[];
};

class PerfDataDumper {
public:
    PerfDataDumper(const char *path) :path(path){
    }
    ~PerfDataDumper() = default;

    int Start(const PmuAttr *pattr) {
        // Layout of perf.data:
        //  ------------------------
        //  |   PerfFileHeader     |
        //  ------------------------
        //  |      event id        |  PerfFileAttr.ids.offset
        //  |      event id        |
        //  |         ...          |
        //  ------------------------  PerfFileHeader.attrs.offset
        //  |     PerfFileAttr     |
        //  |     PerfFileAttr     |
        //  |         ...          |
        //  ------------------------  PerfFileHeader.data.offset
        //  |      perf_event      |
        //  |      perf_event      |
        //  |      perf_event      |
        //  |         ...          |
        //  ------------------------
        //  |    PerfFileSection   |  // the only one feature section which is for build-id
        //  ------------------------  PerfFileSection.offset
        //  |      PerfBuildId     |  // build-id is here
        //  |      PerfBuildId     |  // one module, one build-id struct
        //  |         ...          |
        //  ------------------------

        int err = SUCCESS;
        err = CheckAttr(pattr);
        if (err != SUCCESS) {
            return err;
        }

        fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0644);
        if (fd < 0) {
            return LIBPERF_ERR_OPEN_INVALID_FILE;
        }
        // Calculate essential id header size, used for synthesized events.
        idHdrSize = GetIdHeaderSize();

        // Start to write perf.data, refer to perf_session__write_header in linux.

        // Use perf_magic2 for kernel 5.10.
        long magic2 = 0x32454c4946524550ULL;
        memcpy(ph.magic, &magic2, 8);
        ph.size = sizeof(ph);
        ph.attrSize = sizeof(PerfFileAttr);

        // Header will be written in the very end.
        lseek(fd, sizeof(ph), SEEK_SET);
        // Map event to an ID, used for mapping sample to event.
        // Different from perf tool, we don't calculate hash, but use a simple index.
        // Another difference is that perf tool assign an ID to each event for each core,
        // but we assign an ID to each event for all cores. Maybe it has no impact.
        PrepareEvt2Id(pattr);
        // Write event id and get offset of event id in the file.
        map<const char*, long> evt2offset;
        err = WriteEvtIds(evt2id, fd, evt2offset);
        if (err != SUCCESS) {
            return err;
        }

        // We are going to write PerfFileAttr which contains PmuAttr.
        PerfFileSection attrs = {0};
        attrs.size = sizeof(PerfFileAttr) * pattr->numEvt;
        attrs.offset = lseek(fd, 0, SEEK_CUR);
        // Let header know where PerfFileAttr is.
        ph.attrs = attrs;
        err = WriteFileAttrs(fd, pattr, evt2offset);
        if (err != SUCCESS) {
            return err;
        }

        ph.data.offset = lseek(fd, 0, SEEK_CUR);
        // Going to write synthesized events.
        // When attaching a process, some perf_events are missing, including mmap, comm...
        // These events appeare at the beginning of a process.
        // Then we should synthesize events for better knowing processes.
        err = SynthesizeEvents(pattr);
        return err;
    }

    int Dump(PmuData *data, const int len)
    {
        int err = SUCCESS;
        // Write events like mmap, mmap2, comm, fork...
        err = WriteInfoSamples(fd, data, ph.data.size);
        if (err != SUCCESS) {
            return err;
        }
        // Write PmuData list.
        for (int i = 0; i < len; ++i) {
            auto &d = data[i];
            err = WritePmuData(d);
            if (err != SUCCESS) {
                return err;
            }
        }

        return SUCCESS;
    }

    int End() {
        // Going to write build-id.
        // Refer to perf_header__adds_write in util/header.c
        int err = SUCCESS;
        PerfFileSection featSec = {0};
        // Refer to Layout of perf.data to know why.
        auto secStart = lseek(fd, 0, SEEK_CUR);
        lseek(fd, secStart + sizeof(featSec), SEEK_SET);
        featSec.offset = lseek(fd, 0, SEEK_CUR);

        for (auto &modName : modules) {
            err = WriteBuildId(modName);
            if (err != SUCCESS) {
                return err;
            }
        }
        featSec.size = lseek(fd, 0, SEEK_CUR) - featSec.offset;
        lseek(fd, secStart, SEEK_SET);
        write(fd, &featSec, sizeof(featSec));

        const static size_t HEADER_BUILD_ID = 2;
        // Refer to tools/include/asm-generic/bitops/atomic.h
        // Only build-id is set in features now for pgo.
        ph.addsFeatures[HEADER_BUILD_ID / __BITS_PER_LONG] |= 1UL << (HEADER_BUILD_ID % __BITS_PER_LONG);

        lseek(fd, 0, SEEK_SET);
        err = write(fd, &ph, sizeof(ph));
        if (err < 0) {
            return COMMON_ERR_WRITE;
        }
        close(fd);
        return SUCCESS;
    }

private:
    int CheckAttr(const PmuAttr *pattr)
    {
        if (pattr->numEvt == 0) {
            New(LIBPERF_ERR_NOT_SUPPORT_PMU_FILE, "No events in PmuAttr, maybe it is not sampling PmuAttr");
            return LIBPERF_ERR_NOT_SUPPORT_PMU_FILE;
        }
        if (pattr->period == 0) {
            New(LIBPERF_ERR_NOT_SUPPORT_PMU_FILE, "Period ofr freq is zero, maybe it is not sampling PmuAttr");
            return LIBPERF_ERR_NOT_SUPPORT_PMU_FILE;
        }
        return SUCCESS;
    }

    int WritePmuData(const PmuData &d)
    {
        int err = SUCCESS;
        size_t branchNr = 0;
        if (d.ext && d.ext->nr) {
            branchNr = d.ext->nr;
        }

        PerfSample sample = {0};
        sample.header.type = PERF_RECORD_SAMPLE;
        sample.header.misc = PERF_RECORD_MISC_USER;
        sample.header.size = sizeof(sample) + branchNr * sizeof(perf_branch_entry);
        sample.sampleid = evt2id[d.evt];
        sample.ip = d.stack->symbol->addr;
        sample.tid = d.tid;
        sample.pid = d.pid;
        sample.time = d.ts;
        sample.id = sample.sampleid;
        sample.cpu = d.cpu;
        sample.period = d.period;
        sample.bnr = branchNr;

        modules.insert(d.stack->symbol->module);

        // Write perf sample except brbe data.
        err = write(fd, &sample, sizeof(sample));
        if (err < 0) {
            return COMMON_ERR_WRITE;
        }
        // Write brbe data.
        for (size_t i = 0;i < branchNr; ++i) {
            perf_branch_entry bentry = {0};
            bentry.from = d.ext->branchRecords[i].fromAddr;
            bentry.to = d.ext->branchRecords[i].toAddr;
            bentry.cycles = d.ext->branchRecords[i].cycles;
            bentry.mispred = d.ext->branchRecords[i].misPred;
            bentry.predicted = d.ext->branchRecords[i].predicted;
            err = write(fd, &bentry, sizeof(bentry));
            if (err < 0) {
                return COMMON_ERR_WRITE;
            }
        }
        ph.data.size += sample.header.size;
        return SUCCESS;
    }

    int WriteBuildId(const string &modName)
    {
        int err = SUCCESS;
        char *buildId = nullptr;
        int len = SymGetBuildId(modName.c_str(), &buildId);
        if (len < 0) {
            SetWarn(Perrorno());
            return SUCCESS;
        }
        if (len > BUILD_ID_LEN) {
            // linux says the length of build-id is 20.
            SetWarn(LIBSYM_ERR_BUILDID_TOO_LONG);
            delete buildId;
            return SUCCESS;
        }

        // Refer to function <write_buildid> in linux tools/perf/util/build-id.c
        PerfBuildId bid = {0};
        auto alignedLen = PERF_ALIGN(modName.length(), NAME_ALIGN);
        memcpy(bid.data, buildId, len);
        bid.size = len;
        bid.pid = -1;
        bid.header.misc = PERF_RECORD_MISC_BUILD_ID_SIZE | PERF_RECORD_MISC_USER;
        bid.header.size = sizeof(bid) + alignedLen;
        // Write fields in PerfBuildId except filename.
        err = write(fd, &bid, sizeof(bid));
        if (err < 0) {
            delete buildId;
            return COMMON_ERR_WRITE;
        }

        // Write filename.
        err = write(fd, modName.c_str(), modName.length() + 1);
        if (err < 0) {
            delete buildId;
            return COMMON_ERR_WRITE;
        }
        // Write padding buffer for alignment.
        if (alignedLen > modName.length() + 1) {
            static const char zeroBuf[NAME_ALIGN] = {0};
            err = write(fd, zeroBuf, alignedLen - (modName.length() + 1));
            if (err < 0) {
                delete buildId;
                return COMMON_ERR_WRITE;
            }
        }

        return SUCCESS;
    }

    int SynthesizeEvents(const PmuAttr *pattr)
    {
        int err = SUCCESS;
        for (int i = 0;i < pattr->numPid;++i) {
            int numChild = 0;
            auto childTid = GetChildTid(pattr->pidList[i], &numChild);
            for (int j = 0;j < numChild; ++j) {
                err = SynthesizeCommEvents(fd, childTid[j], ph.data.size);
                if (err != SUCCESS) {
                    return err;
                }
            }
            err = SynthesizeMmapEvents(fd, pattr->pidList[i], ph.data.size);
            if (err != SUCCESS) {
                return err;
            }
        }
        return SUCCESS;
    }

    int SynthesizeMmapEvents(const int fd, const int pid, uint64_t &dataSize)
    {
        // Read /proc/<pid>/maps to get modules info.
        // Refer to perf_event__synthesize_mmap_events in linux.

        static const char annon[] = "//anon";

        string mapFile = "/proc/" + to_string(pid) + "/maps";
        ifstream mapIf(mapFile.c_str());
        constexpr __u64 MAX_LINUX_SYMBOL_LEN = 8192;
        constexpr __u64 MAX_LINUX_MODULE_LEN = 1024;
        char line[MAX_LINUX_SYMBOL_LEN];
        while (mapIf.getline(line, MAX_LINUX_SYMBOL_LEN)) {
            PerfRecordMmap2 *event = (PerfRecordMmap2*)malloc(sizeof(PerfRecordMmap2) + idHdrSize);
            if (event == NULL) {
                return COMMON_ERR_NOMEM;
            }
            event->header.type = PERF_RECORD_MMAP2;
            event->filename[0] = '\0';

            __u64 end = 0;
            string mode;
            string modName;
            // Read some lines like that:
            // 00001234-00001264 r-xp 00000000 fd:01 41234 /lib/libc.so
            auto err = ReadMaps(line, event->addr, end, mode,
                                event->pgoff, event->maj, event->min, event->ino, modName);
            if (err != SUCCESS) {
                free(event);
                return err;
            }
            event->len = end - event->addr;

            // Parse r-xp to event->prot and event->flags.
            err = ParseMapMode(mode, event->prot, event->flags);
            if (err != SUCCESS) {
                free(event);
                return err;
            }
            event->ino_generation = 0;

            // TODO: only support host yet.
            event->header.misc = PERF_RECORD_MISC_USER;
            if ((event->prot & PROT_EXEC) == 0) {
                event->header.misc |= PERF_RECORD_MISC_MMAP_DATA;
            }

            if (modName.empty()) {
                // anonymous memory segment
                strcpy(event->filename, annon);
            } else {
                auto copySize = modName.size() > PATH_MAX - 1 ? PATH_MAX - 1 : modName.size();
                memcpy(event->filename, modName.c_str(), copySize);
                event->filename[copySize] = '\0';
            }

            size_t size = strlen(event->filename) + 1;
            auto alignSize = PERF_ALIGN(size, sizeof(__u64));
            // PerfRecordMmap2 + (real filename size) + (id header size) (maybe we don't need idHdrSize?)
            event->header.size = sizeof(*event) - (sizeof(event->filename) - alignSize) + idHdrSize;
            memset(event->filename + size, 0, idHdrSize + (alignSize - size));
            // pid is actually tid in user space and we need to get his pid.
            event->pid = GetTgid(pid);
            event->tid = pid;

            err = write(fd, event, event->header.size);
            if (err < 0) {
                free(event);
                return COMMON_ERR_WRITE;
            }
            dataSize += event->header.size;
            free(event);
        }

        return SUCCESS;
    }

    int SynthesizeCommEvents(const int fd, const int pid, uint64_t &dataSize)
    {
        PerfRecordComm *event = (PerfRecordComm *)malloc(sizeof(PerfRecordComm) + idHdrSize);
        if (event == NULL) {
            return COMMON_ERR_NOMEM;
        }
        auto comm = GetComm(pid);
        auto size = strlen(comm) > sizeof(event->comm) - 1 ? sizeof(event->comm) - 1 : strlen(comm);
        memcpy(event->comm, comm, size);
        event->comm[size] = '\0';
        event->pid = GetTgid(pid);
        event->header.type = PERF_RECORD_COMM;
        size = PERF_ALIGN(size + 1, sizeof(__u64));
        memset(event->comm + size, 0, idHdrSize);
        // PerfRecordComm + (real comm size) + (id header size) (maybe we don't need idHdrSize?)
        event->header.size = sizeof(*event) - (sizeof(event->comm) - size) + idHdrSize;
        event->tid = pid;

        if (write(fd, event, event->header.size) <0 ) {
            free(event);
            return COMMON_ERR_WRITE;
        }
        dataSize += event->header.size;
        free(event);
        return SUCCESS;
    }

    __u64 GetSampleType()
    {
        return PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_TIME | PERF_SAMPLE_ID |
                PERF_SAMPLE_CPU | PERF_SAMPLE_PERIOD | PERF_SAMPLE_IDENTIFIER | PERF_SAMPLE_BRANCH_STACK;
    }

    PerfFileAttr GetFileAttr(const char *evt, const map<const char*, long> &evt2offset)
    {
        // Now we don't have real perf_event_attr of collection task,
        // then we synthesize a similar one, only for sampling.
        auto pfm = PfmGetPmuEvent(evt, SAMPLING);
        perf_event_attr attr = {0};
        attr.type = pfm->type;
        attr.config = pfm->config;
        attr.sample_period = pfm->period;
        attr.freq = pfm->useFreq;
        attr.exclude_kernel = pfm->excludeKernel;
        attr.exclude_user = pfm->excludeUser;
        // Use a constant sample type, for now, we only support fixed field data, including brbe.
        attr.sample_type = GetSampleType();
        // use attr in 5.10
        attr.size = sizeof(perf_event_attr);
        attr.sample_id_all = 1;
        PerfFileAttr fattr = {0};
        fattr.attr = attr;
        // This is the offset of event id.
        fattr.ids.offset = evt2offset.at(evt);
        fattr.ids.size = sizeof(long);

        return fattr;
    }

    void PrepareEvt2Id(const PmuAttr *pattr)
    {
        long id = 0;
        for (int i = 0; i < pattr->numEvt; ++i) {
            evt2id[pattr->evtList[i]] = id++;
        }
    }

    int WriteEvtIds(const map<const char*, long> &evt2id, const int fd, map<const char*, long> &evt2offset)
    {
        for (auto ei : evt2id) {
            evt2offset[ei.first] = lseek(fd, 0, SEEK_CUR);
            if (write(fd, &ei.second, sizeof(ei.second)) < 0) {
                return COMMON_ERR_WRITE;
            }
        }
        return SUCCESS;
    }

    int WriteFileAttrs(const int fd, const PmuAttr *pattr, const map<const char*, long> &evt2offset)
    {
        for (int i = 0;i < pattr->numEvt; ++i) {
            auto fattr = GetFileAttr(pattr->evtList[i], evt2offset);
            if (write(fd, &fattr, sizeof(fattr)) < 0) {
                return COMMON_ERR_WRITE;
            }
        }

        return SUCCESS;
    }

    int WriteInfoSamples(const int fd, PmuData *data, uint64_t &dataSize)
    {
        const auto &metaData = PmuList::GetInstance()->GetMetaData(data);
        for (auto &sample : metaData) {
            if (write(fd, &sample, sample.header.size) < 0) {
                return COMMON_ERR_WRITE;
            }
            dataSize += sample.header.size;
        }
        return SUCCESS;
    }

    int ReadMaps(const char *line, __u64 &start, __u64 &end, string &mode, 
                    __u64 &offset, __u32 &maj, __u32 &min, __u64 &inode, string &modName)
    {
        constexpr __u64 MAX_LINUX_MODULE_LEN = 1024;
        constexpr int MODE_LEN = 5;
        char modeBf[MODE_LEN] = "";
        char modNameChar[MAX_LINUX_MODULE_LEN] = "";
        if (sscanf(line, "%lx-%lx %s %lx %x:%x %ld %s",
                       &start, &end, modeBf, &offset, &maj, &min,
                       &inode, modNameChar) == EOF) {
            return -1;
        }

        mode = modeBf;
        modName = modNameChar;
        return SUCCESS;
    }

    int ParseMapMode(const string &mode, __u32 &prot, __u32 &flags)
    {
        if (mode.size() != 4) {
            return -1;
        }

        if (mode[0] == 'r') {
            prot |= PROT_READ;
        }
        if (mode[1] == 'w') {
            prot |= PROT_WRITE;
        }
        if (mode[2] == 'x') {
            prot |= PROT_EXEC;
        }
        if (mode[3] == 's') {
            flags |= MAP_SHARED;
        } else if (mode[3] == 'p') {
            flags |= MAP_PRIVATE;
        }

        return SUCCESS;
    }

    unsigned GetIdHeaderSize()
    {
        // Refer to perf_evlist__id_hdr_size in linux.
        unsigned size = 0;
        auto sampleType = GetSampleType();
        if (sampleType & PERF_SAMPLE_TID) {
            size += sizeof(__u32) * 2;
        }
        if (sampleType & PERF_SAMPLE_TIME) {
            size += sizeof(__u64);
        }
        if (sampleType & PERF_SAMPLE_ID) {
            size += sizeof(__u64);
        }
        if (sampleType & PERF_SAMPLE_CPU) {
            size += sizeof(__u32) * 2;
        }
        if (sampleType & PERF_SAMPLE_IDENTIFIER) {
            size += sizeof(__u64);
        }

        return size;
    }

    unsigned idHdrSize = 0;
    const char *path = nullptr;
    PerfFileHeader ph = {0};
    int fd = 0;
    map<const char*, long> evt2id;
    set<string> modules;

    const uint16_t PERF_RECORD_MISC_BUILD_ID_SIZE = (1 << 15);
    const static size_t NAME_ALIGN = 64;
};

map<PmuFile, unique_ptr<PerfDataDumper>> dumpers;

PmuFile PmuBeginWrite(const char *path, const PmuAttr *pattr)
{
    try {
        unique_ptr<PerfDataDumper> dumper(new PerfDataDumper(path));
        int err = dumper->Start(pattr);
        if (err != SUCCESS) {
            New(err);
            return NULL;
        }

        PmuFile fileHandle = new char;
        dumpers[fileHandle] = move(dumper);
        New(SUCCESS);
        return fileHandle;
    } catch (exception& ex) {
        New(UNKNOWN_ERROR, ex.what());
        return NULL;
    }
}

int PmuWriteData(PmuFile file, PmuData *data, int len)
{
    try {
        auto findDumper = dumpers.find(file);
        if (findDumper != dumpers.end()) {
            int err = findDumper->second->Dump(data, len);
            if (err != SUCCESS) {
                New(err);
            }
            return err;
        }
        New(LIBPERF_ERR_INVALID_PMU_FILE);
        return LIBPERF_ERR_INVALID_PMU_FILE;
    } catch (exception& ex) {
        New(UNKNOWN_ERROR, ex.what());
        return UNKNOWN_ERROR;
    }
}

void PmuEndWrite(PmuFile file)
{
    try {
        auto findDumper = dumpers.find(file);
        if (findDumper != dumpers.end()) {
            findDumper->second->End();
            delete (char*)(file);
        }
    } catch (exception& ex) {
        New(UNKNOWN_ERROR, ex.what());
    }
}