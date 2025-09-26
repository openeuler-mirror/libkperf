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
 * Author: Mr.Zhang
 * Create: 2024-04-03
 * Description: function for mapping system errors to custom error codes in the KUNPENG_PMU namespace
 ******************************************************************************/
#include "pcerrc.h"
#include "pmu_event.h"

namespace KUNPENG_PMU {
    int MapErrno(int sysErr)
    {
        switch (sysErr) {
            case EPERM:
            case ENOENT:
                return LIBPERF_ERR_OPEN_INVALID_FILE;
            case EACCES:
                return LIBPERF_ERR_NO_PERMISSION;
            case EBUSY:
                return LIBPERF_ERR_DEVICE_BUSY;
            case EINVAL:
                return LIBPERF_ERR_DEVICE_INVAL;
            case ESRCH:
                return LIBPERF_ERR_NO_PROC;
            case EMFILE:
                return LIBPERF_ERR_TOO_MANY_FD;
            case EOPNOTSUPP:
                return LIBPERF_ERR_INVALID_EVENT;
            default:
                return UNKNOWN_ERROR;
        }
    }
}  // namespace KUNPENG_PMU
