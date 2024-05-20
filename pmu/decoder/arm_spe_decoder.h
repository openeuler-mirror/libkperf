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
 * Author: Mr.Jin
 * Create: 2024-04-03
 * Description: get-spe-data interface and spe-packet definitions
 ******************************************************************************/
#ifndef __SPE__DECODER_HH__
#define __SPE__DECODER_HH__

#include <cstdint>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/perf_event.h>

enum class SpePacketType {
    SPE_PACKET_BAD,
    SPE_PACKET_ADDRESS,
    SPE_PACKET_CONTEXT,
    SPE_PACKET_COUNTER,
    SPE_PACKET_DATA_SOURCE,
    SPE_PACKET_END,
    SPE_PACKET_EVENTS,
    SPE_PACKET_OP_TYPE,
    SPE_PACKET_PAD,
    SPE_PACKET_TIMESTAMP,
};

struct SpePacket {
    enum SpePacketType type;
    uint64_t payload;
    uint16_t header;
    uint16_t payloadSize;
};

struct SpeRecord;

SpeRecord *SpeGetRecord(uint8_t *buf, uint8_t *end, struct SpeRecord *rec, int *remainSize);

#endif
