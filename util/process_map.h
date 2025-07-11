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
 * Author: Mr.Wang
 * Create: 2024-04-03
 * Description: Get process and thread information.
 ******************************************************************************/
#ifndef PROCESS_MAP_H
#define PROCESS_MAP_H
#include <linux/types.h>
#include <symbol.h>
#ifdef __cplusplus
extern "C" {
#endif

struct ProcTopology* GetProcTopology(pid_t pid);
void FreeProcTopo(struct ProcTopology *procTopo);
int* GetChildTid(int pid, int* numChild);
int GetTgid(pid_t pid);
char *GetComm(pid_t pid);

#ifdef __cplusplus
}
#endif
#endif