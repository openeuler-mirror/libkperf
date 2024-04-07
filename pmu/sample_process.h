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
 * Author: Mr.Gan
 * Create: 2024-04-03
 * Description: definition of functions for handling performance event sampling processes in
 * the KUNPENG_PMU namespace
 ******************************************************************************/
#ifndef PMU_SAMPLE_PROCESS_H
#define PMU_SAMPLE_PROCESS_H
#include <memory>
#include "pmu_event.h"

namespace KUNPENG_PMU {

    int MmapInit(PerfMmap& sampleMmap);
    union PerfEvent* ReadEvent(PerfMmap& map);
    int RingbufferReadInit(PerfMmap& map);
    void PerfMmapConsume(PerfMmap& map);
    void PerfMmapReadDone(PerfMmap& map);
    int CreateRingbuffer(PerfMmap& map, int mask, int prot, int fd);

}  // namespace KUNPENG_PMU

#endif
