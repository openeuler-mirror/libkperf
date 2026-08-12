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
 * Author: Wu
 * Create: 2026-05-28
 * Description: Immutable identifier for a Java method consisting of owner class, method name, and descriptor
 ******************************************************************************/
package com.libkperf.tracex.agent;

public final class MethodId {
    public final String owner;
    public final String name;
    public final String desc;

    public MethodId(String owner, String name, String desc) {
        this.owner = owner == null ? "" : owner;
        this.name = name == null ? "" : name;
        this.desc = desc == null ? "" : desc;
    }

    public String func() {
        return name + desc;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof MethodId)) return false;
        MethodId that = (MethodId) o;
        return owner.equals(that.owner) && name.equals(that.name) && desc.equals(that.desc);
    }

    @Override
    public int hashCode() {
        int result = owner.hashCode();
        result = 31 * result + name.hashCode();
        result = 31 * result + desc.hashCode();
        return result;
    }

    @Override
    public String toString() {
        return owner + "::" + name + desc;
    }
}
