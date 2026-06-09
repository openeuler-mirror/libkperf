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

import java.io.BufferedReader;
import java.io.FileInputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

public final class TraceConfig {
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
        this.shmPath = get(args, "shmPath", "/tmp/utrace-java-default.shm");
        this.slotCount = parseInt(get(args, "slotCount", "65536"), 65536);
        this.nativeLibPath = get(args, "nativeLibPath", "");
        this.action = get(args, "action", "start");
        this.includeFile = get(args, "includeFile", "");
        this.configFile = get(args, "configFile", "");

        TraceFilterFile filterFile = TraceFilterFile.load(configFile);
        int argDepth = parseInt(get(args, "contextDepth", "-1"), -1);
        int argMax = parseInt(get(args, "contextMaxMethods", "-1"), -1);

        this.contextDepth = argDepth >= 0
                ? argDepth
                : (filterFile.contextDepth >= 0 ? filterFile.contextDepth : 1);

        this.contextMaxMethods = argMax >= 0
                ? argMax
                : (filterFile.contextMaxMethods >= 0 ? filterFile.contextMaxMethods : 128);

        String includeAllArg = get(args, "includeAll", null);
        this.includeAll = includeAllArg == null ? filterFile.includeAll : "true".equals(includeAllArg.trim());

        List<FilterRule> required = new ArrayList<FilterRule>();
        addRules(required, readIncludeFile(includeFile));
        addRules(required, get(args, "hotRules", ""));
        addRules(required, get(args, "requiredIncludes", ""));
        addRules(required, get(args, "includeRules", ""));

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
        if (matchesRequiredIncludeClass(classNameInternal)) {
            return true;
        }
        if (matchesConfigIncludeClass(classNameInternal)) {
            return true;
        }
        if (hasContextMethodInClass(classNameInternal)) {
            return true;
        }
        return includeAll;
    }

    public boolean shouldTransformMethod(String owner, String name, String desc) {
        if (isExcludedMethod(owner, name, desc)) {
            return false;
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
        return includeAll;
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
        return includeAll;
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

    private boolean isExcludedClass(String owner) {
        for (FilterRule r : excludeRules) {
            if (r.matchesClass(owner)) {
                return true;
            }
        }
        return false;
    }

    private boolean isExcludedMethod(String owner, String name, String desc) {
        for (FilterRule r : excludeRules) {
            if (r.matchesMethod(owner, name, desc)) {
                return true;
            }
        }
        return false;
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

    private static void addRules(List<FilterRule> out, String text) {
        if (text == null || text.length() == 0) {
            return;
        }

        String[] parts = text.split("[,\\n\\r]");

        for (String p : parts) {
            FilterRule r = FilterRule.parse(p);
            if (r != null) {
                out.add(r);
            }
        }
    }

    private static String readIncludeFile(String path) {
        if (path == null || path.length() == 0) {
            return "";
        }

        StringBuilder sb = new StringBuilder();

        try (BufferedReader br = new BufferedReader(
                new InputStreamReader(new FileInputStream(path), StandardCharsets.UTF_8))) {
            String line;
            while ((line = br.readLine()) != null) {
                sb.append(line).append('\n');
            }
        } catch (Exception e) {
            System.err.println("[trace-java-agent] read include file failed: " + path + ", " + e);
        }
        return sb.toString();
    }

    private static String get(Map<String, String> m, String k, String d) {
        String v = m.get(k);
        return v == null ? d : v;
    }

    private static int parseInt(String s, int d) {
        try {
            return Integer.parseInt(s);
        } catch (Exception e) {
            return d;
        }
    }
}
