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
 * Create: 2026-04-27
 * Description: Parses and holds the trace configuration: shared memory path, slot count, filter rules, etc.
 ******************************************************************************/
package com.libkperf.tracex.agent;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

public final class TraceConfig {
    private static final int DEFAULT_SLOT_COUNT = 1048576;
    private static final int MAX_SLOT_COUNT = 67108864;
    private static final String AGENT_PACKAGE_PREFIX = "com/libkperf/tracex/";

    public final String shmPath;
    public final int slotCount;
    public final String nativeLibPath;
    public final String action;
    public final String includeFile;
    public final String configFile;
    public final int contextDepth;
    public final int contextMaxMethods;
    public final boolean includeAll;

    public final List<FilterRule> requiredIncludeRules;
    public final List<FilterRule> includeRules;
    public final List<FilterRule> excludeRules;

    private volatile Set<MethodId> contextMethods = Collections.emptySet();

    private TraceConfig(Map<String, String> args) {
        this.shmPath = Util.get(args, "shmPath", "/tmp/utrace-java-default.shm");
        this.nativeLibPath = Util.get(args, "nativeLibPath", "");
        this.action = Util.get(args, "action", "start");
        this.includeFile = Util.get(args, "includeFile", "");
        this.configFile = Util.get(args, "configFile", "");

        TraceFilterFile filterFile = TraceFilterFile.load(configFile);
        this.slotCount = clampSlotCount(filterFile.slotCount > 0 ? filterFile.slotCount : DEFAULT_SLOT_COUNT);
        this.contextMaxMethods = filterFile.contextMaxMethods >= 0 ? filterFile.contextMaxMethods : 128;

        this.includeAll = filterFile.includeAll;
        int configuredContextDepth = filterFile.contextDepth >= 0 ? filterFile.contextDepth : 1;
        this.contextDepth = this.includeAll ? 0 : configuredContextDepth;

        List<FilterRule> required = new ArrayList<FilterRule>();
        Util.addRules(required, Util.readTextFile(includeFile, "[trace-java-agent] read include file failed: "));
        Util.addRules(required, Util.get(args, "hotRules", ""));
        Util.addRules(required, Util.get(args, "requiredIncludes", ""));
        Util.addRules(required, Util.get(args, "includeRules", ""));

        this.requiredIncludeRules = Collections.unmodifiableList(required);

        List<FilterRule> inc = new ArrayList<FilterRule>();
        inc.addAll(filterFile.includes);
        this.includeRules = Collections.unmodifiableList(inc);

        List<FilterRule> exc = new ArrayList<FilterRule>();
        exc.addAll(filterFile.excludes);
        this.excludeRules = Collections.unmodifiableList(exc);
    }

    public static TraceConfig parse(String agentArgs) {
        return new TraceConfig(parseKv(agentArgs));
    }

    public boolean isStopAction() {
        return "stop".equalsIgnoreCase(action);
    }

    public boolean isRestoreAction() {
        return "restore".equalsIgnoreCase(action);
    }

    public boolean shouldTransformClass(String classNameInternal) {
        if (isExcludedClass(classNameInternal)) {
            return false;
        }
        if (includeAll) {
            return true;
        }
        if (matchesRequiredIncludeClass(classNameInternal)) {
            return true;
        }
        if (matchesConfigIncludeClass(classNameInternal)) {
            return true;
        }
        if (hasContextMethodInClass(classNameInternal)) {
            return true;
        }
        return false;
    }

    public boolean shouldTransformMethod(String owner, String name, String desc) {
        if (isExcludedMethod(owner, name, desc)) {
            return false;
        }
        if (includeAll) {
            return true;
        }
        if (matchesRequiredIncludeMethod(owner, name, desc)) {
            return true;
        }
        if (matchesConfigIncludeMethod(owner, name, desc)) {
            return true;
        }
        if (contextMethods.contains(new MethodId(owner, name, desc))) {
            return true;
        }
        return false;
    }

    public boolean matchesUserInclude(String owner, String name, String desc) {
        if (isExcludedMethod(owner, name, desc)) {
            return false;
        }
        if (matchesRequiredIncludeMethod(owner, name, desc)) {
            return true;
        }
        if (matchesConfigIncludeMethod(owner, name, desc)) {
            return true;
        }
        return false;
    }

    public void addContextMethods(Set<MethodId> methods) {
        if (methods == null || methods.isEmpty()) {
            return;
        }
        Set<MethodId> merged = new HashSet<MethodId>(contextMethods);
        merged.addAll(methods);
        contextMethods = Collections.unmodifiableSet(merged);
    }

    private boolean matchesRequiredIncludeClass(String owner) {
        for (FilterRule r : requiredIncludeRules) {
            if (r.matchesClass(owner)) {
                return true;
            }
        }
        return false;
    }

    private boolean matchesRequiredIncludeMethod(String owner, String name, String desc) {
        for (FilterRule r : requiredIncludeRules) {
            if (r.matchesMethod(owner, name, desc)) {
                return true;
            }
        }
        return false;
    }

    private boolean matchesConfigIncludeClass(String owner) {
        for (FilterRule r : includeRules) {
            if (r.matchesClass(owner)) {
                return true;
            }
        }
        return false;
    }

    private boolean matchesConfigIncludeMethod(String owner, String name, String desc) {
        for (FilterRule r : includeRules) {
            if (r.matchesMethod(owner, name, desc)) {
                return true;
            }
        }
        return false;
    }

    private boolean hasContextMethodInClass(String owner) {
        Set<MethodId> ctx = contextMethods;
        for (MethodId id : ctx) {
            if (id.owner.equals(owner)) {
                return true;
            }
        }
        return false;
    }

    boolean isExcludedClass(String owner) {
        if (isHardExcludedOwner(owner)) {
            return true;
        }
        for (FilterRule r : excludeRules) {
            if (r.matchesClass(owner)) {
                return true;
            }
        }
        return false;
    }

    boolean isExcludedMethod(String owner, String name, String desc) {
        if (isHardExcludedOwner(owner)) {
            return true;
        }
        for (FilterRule r : excludeRules) {
            if (r.matchesMethod(owner, name, desc)) {
                return true;
            }
        }
        return false;
    }

    private static boolean isHardExcludedOwner(String owner) {
        if (owner == null || owner.length() == 0) {
            return true;
        }
        return owner.replace('.', '/').startsWith(AGENT_PACKAGE_PREFIX);
    }

    private static Map<String, String> parseKv(String agentArgs) {
        Map<String, String> out = new HashMap<String, String>();
        if (agentArgs == null || agentArgs.length() == 0) {
            return out;
        }
        StringBuilder key = new StringBuilder();
        StringBuilder val = new StringBuilder();
        StringBuilder cur = key;
        boolean esc = false;
        for (int i = 0; i <= agentArgs.length(); i++) {
            char c = i == agentArgs.length() ? ';' : agentArgs.charAt(i);
            if (esc) {
                cur.append(c);
                esc = false;
            } else if (c == '\\') {
                esc = true;
            } else if (c == '=' && cur == key) {
                cur = val;
            } else if (c == ';') {
                if (key.length() > 0) {
                    out.put(key.toString(), val.toString());
                }
                key.setLength(0);
                val.setLength(0);
                cur = key;
            } else {
                cur.append(c);
            }
        }
        return out;
    }

    private static int clampSlotCount(long value) {
        if (value < DEFAULT_SLOT_COUNT) {
            return DEFAULT_SLOT_COUNT;
        }
        if (value > MAX_SLOT_COUNT) {
            return MAX_SLOT_COUNT;
        }
        return (int) value;
    }
}
