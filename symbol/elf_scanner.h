/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * libkperf licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: Xie Jingwei
 * Create: 2026-01-21
 * Description: Interfaces for parsing ELF binaries and extracting function probe points
 ******************************************************************************/

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <elf.h>

struct ProbePoints {
    std::string symbolName;
    uint64_t entryOffset;              // Function entry offset relative to base
    std::vector<uint64_t> retOffsets;  // All ret instruction offsets relative to base
};

// File mapping class
class MappedFile {
public:
    explicit MappedFile(const std::string &path);

    ~MappedFile();

    MappedFile(const MappedFile &) = delete;

    MappedFile &operator=(const MappedFile &) = delete;

    void *GetBase() const;

    size_t GetSize() const;

private:
    int fd_;
    size_t size_;
    void *base_;
};

class ElfScanner {
public:
    static std::unordered_map<std::string, std::vector<ProbePoints>> ResolveElfs(
        const std::unordered_map<std::string, std::vector<std::string>> &module2Symbols);

    static std::string FormatFailures();

private:
    struct ElfSymEntry {
        uint64_t value;
        uint64_t size;
        uint64_t shndx;
    };

    static std::unordered_map<std::string, std::string> failedElf2Reason_;

    static std::unordered_map<std::string, std::vector<std::string>> elf2FailedSymbols_;

    static const Elf64_Ehdr *VerifyElfHeader(const char *base, size_t fileSize);

    static uint64_t CalLoadBaseAddr(const Elf64_Ehdr *ehdr, const Elf64_Phdr *phdr);

    static std::unordered_map<std::string, ElfSymEntry> ExtractSymEntries(const Elf64_Ehdr *ehdr,
        const Elf64_Shdr *shdr, const char *base, const std::unordered_set<std::string> &targetSet);

    static uint64_t GetSymFileOffset(const ElfSymEntry &symbol, const Elf64_Shdr *shdr);

    static std::vector<uint64_t> GetRetInstOffsets(const ElfSymEntry &symEntry, const char *base, size_t fileSize,
        uint64_t baseVirtualAddr, uint64_t symbolFileOffset, uint16_t machineType);

    static ProbePoints ConstructProbePoints(const std::string &name, const ElfSymEntry &symbol, const Elf64_Shdr *shdr,
        const char *base, size_t fileSize, uint64_t baseVirtualAddr, uint16_t machineType);

    static std::vector<ProbePoints> ResolveElf(
        const std::string &filePath, const std::vector<std::string> &symbolsToFind);
};