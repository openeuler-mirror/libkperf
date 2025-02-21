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
 * Description: parses the SPE data in the AUX buffer
 ******************************************************************************/
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "spe.h"
#include "arm_spe_decoder.h"

static void SetPktPayload(struct SpePacket *pkt, uint8_t *buf)
{
    switch (pkt->payloadSize) {
        case 1:
            pkt->payload = *buf;
            break;
        case 2:
            pkt->payload = *(uint16_t *)(buf);
            break;
        case 4:
            pkt->payload = *(uint32_t *)(buf);
            break;
        case 8:
            pkt->payload = *(uint64_t *)(buf);
            break;
        default:
            break;
    }
}

static inline void* SpePacketPad(struct SpePacket *pkt, uint8_t *buf)
{
    pkt->type = SpePacketType::SPE_PACKET_PAD;
    buf += sizeof(uint8_t);
    return buf;
}

static inline void* SpePacketEnd(struct SpePacket *pkt, uint8_t *buf)
{
    pkt->type = SpePacketType::SPE_PACKET_END;
    buf += sizeof(uint8_t);
    return buf;
}

static inline void* SpePacketTs(uint16_t header, struct SpePacket *pkt, uint8_t *buf)
{
    pkt->type = SpePacketType::SPE_PACKET_TIMESTAMP;
    buf += sizeof(uint8_t);
    pkt->payloadSize = 1 << ((header & 0b110000) >> 4);
    SetPktPayload(pkt, buf);
    buf += pkt->payloadSize;
    return buf;
}

static uint8_t* Get0B01Pkt(uint16_t header, struct SpePacket *pkt, uint8_t *buf)
{
    if ((header & 0b1111) == 0b0010) {
        pkt->type = SpePacketType::SPE_PACKET_EVENTS;
        buf += sizeof(uint8_t);
        pkt->payloadSize = 1 << ((header & 0b110000) >> 4);
        SetPktPayload(pkt, buf);
        buf += pkt->payloadSize;
    } else if ((header & 0b1111) == 0b0011) {
        pkt->type = SpePacketType::SPE_PACKET_DATA_SOURCE;
        buf += sizeof(uint8_t);
        pkt->payloadSize = 1 << ((header & 0b110000) >> 4);
        buf += pkt->payloadSize;
    } else if ((header >> 2) == 0b011001) {
        pkt->type = SpePacketType::SPE_PACKET_CONTEXT;
        buf += sizeof(uint8_t);
        pkt->payloadSize = 1 << ((header & 0b110000) >> 4);
        SetPktPayload(pkt, buf);
        buf += pkt->payloadSize;
    } else if ((header >> 2) == 0b010010) {
        pkt->type = SpePacketType::SPE_PACKET_OP_TYPE;
        buf += sizeof(uint8_t);
        buf += sizeof(uint8_t);
    } else {
        pkt->type = SpePacketType::SPE_PACKET_BAD;
        buf += sizeof(uint8_t);
    }

    return buf;
}

static uint8_t *GetPkt(struct SpePacket *pkt, uint8_t *buf)
{
    uint16_t header = *static_cast<uint8_t *>(buf);

    switch (header) {
        case (0):
            return static_cast<uint8_t*>(SpePacketPad(pkt, buf));
        case (1):
            return static_cast<uint8_t*>(SpePacketEnd(pkt, buf));
        case (0b01110001):
            return static_cast<uint8_t*>(SpePacketTs(header, pkt, buf));
        default:
            break;
    }

    if (!((header >> 6) ^ 0b01)) {
        buf = Get0B01Pkt(header, pkt, buf);
    } else if ((header >> 3) == 0b10110) {
        pkt->type = SpePacketType::SPE_PACKET_ADDRESS;
        buf += sizeof(uint8_t);
        pkt->payloadSize = 1 << ((header & 0b110000) >> 4);
        SetPktPayload(pkt, buf);
        buf += pkt->payloadSize;
    } else if ((header >> 3) == 0b10011) {
        pkt->type = SpePacketType::SPE_PACKET_COUNTER;
        buf += sizeof(uint8_t);
        pkt->payloadSize = 1 << ((header & 0b110000) >> 4);
        SetPktPayload(pkt, buf);
        buf += pkt->payloadSize;
    } else if ((header >> 10) == 0b001000) {
        header = *(uint16_t *)(buf);
        if ((header & 0b11111000) == 0b10110000) {
            pkt->type = SpePacketType::SPE_PACKET_ADDRESS;
        } else if ((header & 0b11111000) == 0b10011000) {
            pkt->type = SpePacketType::SPE_PACKET_COUNTER;
        } else {
            pkt->type = SpePacketType::SPE_PACKET_BAD;
        }
        buf += sizeof(uint16_t);
        pkt->payloadSize = 1 << ((header & 0b110000) >> 4);
        SetPktPayload(pkt, buf);
        buf += pkt->payloadSize;
    } else {
        pkt->type = SpePacketType::SPE_PACKET_BAD;
        buf += sizeof(uint8_t);
    }

    pkt->header = header;
    return buf;
}

static uint64_t FixupTopByte(uint64_t va)
{
    uint64_t fixup = (va & 0xff000000000000) >> 48;

    /*
     * Armv8 ARM (ARM DDI 0487F.c), chapter "D10.2.1 Address packet"
     * defines the data virtual address payload format, the top byte
     * (bits [63:56]) is assigned as top-byte tag; so we only can
     * retrieve address value from bits [55:0].
     *
     * According to Documentation/arm64/memory.rst, if detects the
     * specific pattern in bits [55:52] of payload which falls in
     * the kernel space, should fixup the top byte.
     *
     * For this reason, if detects the bits [55:52] is 0xf, will
     * fill 0xff into the top byte.
     */
    if ((fixup & 0xf0ULL) == 0xf0ULL) {
        va |= 0xffULL << 56;
    }

    return va;
}

static void DecodeAddressPkt(struct SpePacket *pkt, struct SpeRecord *record)
{
    uint16_t index = (pkt->header & 0b111) | (((pkt->header >> 8) & 0b11) << 3);

    switch (index) {
        case 0:  // PC
            record->pc = pkt->payload & 0xffffffffffffff;
            record->pc = FixupTopByte(record->pc);
            break;
        case 1:  // Branch target address
            break;
        case 2:  // Data access virtual address
            record->va = pkt->payload & 0xffffffffffffff;
            record->va = FixupTopByte(record->va);
            break;
        case 3:  // Data access physical address
            record->pa = pkt->payload & 0xffffffffffffff;
            break;
        case 4:  // Previous branch target address
            break;
        default:
            break;
    }
}

static void DecodeCounterPkt(struct SpePacket *pkt, struct SpeRecord *record)
{
    uint16_t index = (pkt->header & 0b111) | (((pkt->header >> 8) & 0b11) << 3);
    if (index == 0) {
        record->lat = pkt->payload;
    }
}       

static void DecodeEventPkt(struct SpePacket *pkt, struct SpeRecord *record)
{
    record->event = pkt->payload;
}

static void DecodeContextPkt(struct SpePacket *pkt, struct SpeRecord *record)
{
    record->tid = pkt->payload;
}

static void DecodeTimestampPkt(struct SpePacket *pkt, struct SpeRecord *record)
{
    record->timestamp = pkt->payload;
}

static void DecodePkt(struct SpePacket *pkt, struct SpeRecord *record)
{
    switch (pkt->type) {
        case SpePacketType::SPE_PACKET_ADDRESS:
            DecodeAddressPkt(pkt, record);
            break;
        case SpePacketType::SPE_PACKET_CONTEXT:
            DecodeContextPkt(pkt, record);
            break;
        case SpePacketType::SPE_PACKET_COUNTER:
            DecodeCounterPkt(pkt, record);
            break;
        case SpePacketType::SPE_PACKET_DATA_SOURCE:
            break;
        case SpePacketType::SPE_PACKET_END:
            break;
        case SpePacketType::SPE_PACKET_EVENTS:
            DecodeEventPkt(pkt, record);
            break;
        case SpePacketType::SPE_PACKET_OP_TYPE:
            break;
        case SpePacketType::SPE_PACKET_PAD:
            break;
        case SpePacketType::SPE_PACKET_TIMESTAMP:
            DecodeTimestampPkt(pkt, record);
            break;
        default:
            break;
    }
}

SpeRecord *SpeGetRecord(uint8_t *buf, uint8_t *end, struct SpeRecord *rec, int *remainSize)
{
    struct SpePacket pkt;

    rec->pid = -1;
    rec->tid = -1;
    while (buf < end) {
        if (*remainSize < 1) {
            break;
        }

        buf = GetPkt(&pkt, buf);
        DecodePkt(&pkt, rec);
        if (pkt.type == SpePacketType::SPE_PACKET_END || pkt.type == SpePacketType::SPE_PACKET_TIMESTAMP) {
            rec++;
            *remainSize -= 1;
            rec->pid = -1;
            rec->tid = -1;
        }
    }

    return rec;
}
