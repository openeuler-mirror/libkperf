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
 * Description: Parses and holds the trace configuration: shared memory path, ring sizing, filter rules, etc.
 ******************************************************************************/
package com.libkperf.tracex.agent;

import com.libkperf.tracex.runtime.TraceLog;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

import com.libkperf.tracex.agent.asm.VTStageInstrumentation;

public final class TraceConfig {
    // Must match SharedEventSink and the pmu/trace java backend defaults.
    private static final int DEFAULT_SLOTS_PER_RING = 327680;

    public final String shmPath;
    public final int slotsPerRing;
    public final String nativeLibPath;
    public final String action;
    public final String includeFile;
    public final String configFile;
    public final boolean valid;
    public final String error;
    public final int contextDepth;
    public final int contextMaxMethods;
    public final boolean enableVTStage;

    public final List<FilterRule> requiredIncludeRules;
    public final List<FilterRule> includeRules;
    public final List<FilterRule> excludeRules;

    private volatile Set<MethodId> contextMethods = Collections.emptySet();
    private final Set<FilterRule> matchedIncludeRules = Collections.synchronizedSet(new HashSet<FilterRule>());
    private final Set<FilterRule> matchedExcludeRules = Collections.synchronizedSet(new HashSet<FilterRule>());

    private TraceConfig(Map<String, String> args) {
        this.shmPath = Util.get(args, "shmPath", "/tmp/utrace-java-default.shm");
        this.nativeLibPath = Util.get(args, "nativeLibPath", "");
        this.action = Util.get(args, "action", "start");
        this.includeFile = Util.get(args, "includeFile", "");
        this.configFile = Util.get(args, "configFile", "");
        TraceLog.init(Util.get(args, "logFile", ""));

        TraceFilterFile filterFile = isControlAction() ? new TraceFilterFile() : TraceFilterFile.load(configFile);
        TraceLog.setLevelByName(filterFile.logLevel);
        this.valid = isControlAction() || filterFile.valid;
        this.error = filterFile.error;
        // TraceFilterFile already validates slots_per_ring in [1, 1 << 22].
        this.slotsPerRing = filterFile.slotsPerRing > 0
                ? (int) filterFile.slotsPerRing
                : DEFAULT_SLOTS_PER_RING;
        this.contextMaxMethods = filterFile.contextMaxMethods >= 0 ? filterFile.contextMaxMethods : 128;

        int configuredContextDepth = filterFile.contextDepth >= 0 ? filterFile.contextDepth : 1;
        this.contextDepth = configuredContextDepth;

        String vtStageArg = Util.get(args, "enableVTStage", "");
        boolean configuredVTStage = vtStageArg.length() > 0
                ? Boolean.parseBoolean(vtStageArg)
                : filterFile.enableVTStage;
        int jdkMajor = jdkMajorVersion();
        this.enableVTStage = configuredVTStage && jdkMajor == 21;
        if (configuredVTStage && jdkMajor != 21) {
            TraceLog.warn("[trace-java-agent] enableVTStage requested but JDK major version is "
                    + jdkMajor + ", virtual thread stage instrumentation requires exactly JDK 21, skipped");
        }

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

    private boolean isControlAction() {
        return isStopAction() || isRestoreAction();
    }

    public boolean isRestoreAction() {
        return "restore".equalsIgnoreCase(action);
    }

    public boolean shouldTransformClass(String classNameInternal) {
        if (enableVTStage && VTStageInstrumentation.isTargetClass(classNameInternal)) {
            return true;
        }
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
        return false;
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
        boolean matched = false;
        for (FilterRule r : includeRules) {
            if (r.matchesClass(owner)) {
                matched = true;
            }
        }
        return matched;
    }

    private boolean matchesConfigIncludeMethod(String owner, String name, String desc) {
        boolean matched = false;
        for (FilterRule r : includeRules) {
            if (r.matchesMethod(owner, name, desc)) {
                matchedIncludeRules.add(r);
                matched = true;
            }
        }
        return matched;
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
        boolean matched = false;
        for (FilterRule r : excludeRules) {
            if (r.isClassOnly() && r.matchesClass(owner)) {
                matchedExcludeRules.add(r);
                matched = true;
            }
        }
        return matched;
    }

    boolean isExcludedMethod(String owner, String name, String desc) {
        boolean matched = false;
        for (FilterRule r : excludeRules) {
            if (r.matchesMethod(owner, name, desc)) {
                matchedExcludeRules.add(r);
                matched = true;
            }
        }
        return matched;
    }

    public String filterRuleStatus() {
        StringBuilder effective = new StringBuilder();
        StringBuilder ineffective = new StringBuilder();
        appendRuleStatus(effective, ineffective, includeRules, matchedIncludeRules, "include");
        appendRuleStatus(effective, ineffective, excludeRules, matchedExcludeRules, "exclude");
        return "matched=[" + effective + "], unmatched=[" + ineffective + "]";
    }

    private static void appendRuleStatus(StringBuilder effective, StringBuilder ineffective,
                                         List<FilterRule> rules, Set<FilterRule> matched, String type) {
        for (FilterRule rule : rules) {
            StringBuilder target = matched.contains(rule) ? effective : ineffective;
            if (target.length() > 0) {
                target.append(", ");
            }
            target.append(type).append(':').append(rule.configuredText());
        }
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

    private static int jdkMajorVersion() {
        String spec = System.getProperty("java.specification.version", "");
        if (spec == null || spec.length() == 0) {
            return 0;
        }
        String trimmed = spec.trim();
        try {
            int dot = trimmed.indexOf('.');
            if (dot < 0) {
                return Integer.parseInt(trimmed);
            }
            if (trimmed.startsWith("1.")) {
                return Integer.parseInt(trimmed.substring(2).trim());
            }
            return Integer.parseInt(trimmed.substring(0, dot).trim());
        } catch (NumberFormatException e) {
            return 0;
        }
    }

}
