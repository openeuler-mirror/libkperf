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
 * Author: Wu
 * Create: 2025-10-21
 * Description: Collecting l2 icache miss, l2d tlb miss of process
 ******************************************************************************/
#include "collect_args.h"
#include "collect.h"

int main(int argc, char* argv[])
{
    CollectArgs args;
    if (!args.ParseOption(argc, argv)) {
        return EXIT_FAILURE;
    }

    collectMiss(args);
    collectSummaryData(args);
    return 0;
}
