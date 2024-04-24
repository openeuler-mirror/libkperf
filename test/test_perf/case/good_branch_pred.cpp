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
 * Author: Mr.Gan
 * Create: 2024-04-26
 * Description: A loop with a predictable condition.
 ******************************************************************************/
#include <random>
#include <unistd.h>

int main()
{
	int len = 10000000;
	int *cond = new int[len];
	for (int i = 0; i < len; ++i) {
		cond[i] = i;
	}
	int sum = 0;
	for (int i = 0; i < 10000000;++i)
	{
		if (cond[i] % 4) {
			sum++;
		} else {
			sum--;
		}
	}

	return 0;
}
