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
 * Description: CLI entry point that attaches the trace agent to a running JVM process
 ******************************************************************************/
package com.libkperf.tracex.cli;

import java.io.File;
import java.lang.reflect.Method;
import java.net.URL;
import java.net.URLClassLoader;
import java.util.HashMap;
import java.util.Map;

public final class TraceCliMain {
    private TraceCliMain() {}

    private static String required(Map<String, String> opts, String key) {
        String v = opts.get(key);
        if (v == null || v.length() == 0) {
            throw new IllegalArgumentException("missing required option " + key);
        }
        return v;
    }

    private static String escape(String value) {
        return value.replace("\\", "\\\\").replace(";", "\\;").replace("=", "\\=");
    }

    private static void appendArg(StringBuilder sb, String key, String value) {
        if (value == null || value.length() == 0) {
            return;
        }
        if (sb.length() > 0) {
            sb.append(';');
        }
        sb.append(key).append('=').append(escape(value));
    }

    private static Map<String, String> parseArgs(String[] args) {
        Map<String, String> out = new HashMap<String, String>();
        for (int i = 0; i < args.length; i++) {
            String key = args[i];
            if (!key.startsWith("-")) {
                throw new IllegalArgumentException("unexpected argument: " + key);
            }
            if (i + 1 >= args.length || args[i + 1].startsWith("-")) {
                throw new IllegalArgumentException("missing value for " + key);
            }
            out.put(key, args[++i]);
        }
        return out;
    }

    public static void main(String[] args) throws Exception {
        Map<String, String> opts = parseArgs(args);
        String pid = required(opts, "-p");
        String agentJar = required(opts, "--agent-jar");
        String action = opts.containsKey("--action") ? opts.get("--action") : "start";
        StringBuilder agentArgs = new StringBuilder();
        appendArg(agentArgs, "action", action);
        if ("start".equals(action)) {
            appendArg(agentArgs, "slotCount", opts.get("--slot-count"));
            appendArg(agentArgs, "shmPath", opts.get("--shm-path"));
            appendArg(agentArgs, "nativeLibPath", opts.get("--native-lib"));
            appendArg(agentArgs, "includeFile", opts.get("--include-file"));
            appendArg(agentArgs, "configFile", opts.get("--config-file"));
            appendArg(agentArgs, "contextDepth", opts.get("--context-depth"));
            appendArg(agentArgs, "contextMaxMethods", opts.get("--context-max-methods"));
        }
        System.out.println("Agent arguments: " + agentArgs.toString());

        VirtualMachineAccess vmAccess = VirtualMachineAccess.load();
        Object vm = null;
        try {
            vm = vmAccess.attach(pid);
            vmAccess.loadAgent(vm, agentJar, agentArgs.toString());
        } finally {
            if (vm != null) {
                vmAccess.detach(vm);
            }
        }
    }

    private static final class VirtualMachineAccess {
        private final Class<?> vmClass;
        private final Method attach;
        private final Method loadAgent;
        private final Method detach;

        private VirtualMachineAccess(Class<?> vmClass) throws Exception {
            this.vmClass = vmClass;
            this.attach = vmClass.getMethod("attach", String.class);
            this.loadAgent = vmClass.getMethod("loadAgent", String.class, String.class);
            this.detach = vmClass.getMethod("detach");
        }

        static VirtualMachineAccess load() throws Exception {
            try {
                return new VirtualMachineAccess(Class.forName("com.sun.tools.attach.VirtualMachine"));
            } catch (ClassNotFoundException ignored) {
            }

            // JDK8 common case when running with plain java -jar: attach classes live in tools.jar.
            File toolsJar = findToolsJar();
            if (toolsJar != null) {
                ClassLoader loader = new URLClassLoader(new URL[] { toolsJar.toURI().toURL() }, TraceCliMain.class.getClassLoader());
                return new VirtualMachineAccess(Class.forName("com.sun.tools.attach.VirtualMachine", true, loader));
            }
            throw new ClassNotFoundException("JVM attach API not found. For JDK8 run with a JDK, not a JRE, "
                + "or put $JAVA_HOME/lib/tools.jar on the classpath.");
        }

        private Object attach(String pid) throws Exception {
            return attach.invoke(null, pid);
        }

        private void loadAgent(Object vm, String agentJar, String args) throws Exception {
            loadAgent.invoke(vm, agentJar, args == null ? "" : args);
        }

        private void detach(Object vm) throws Exception {
            detach.invoke(vm);
        }

        private static File findToolsJar() {
            String javaHome = System.getProperty("java.home");
            File[] candidates = new File[] {
                new File(javaHome, "lib/tools.jar"),
                new File(javaHome, "../lib/tools.jar"),
                new File(System.getenv("JAVA_HOME") == null ? "" : System.getenv("JAVA_HOME"), "lib/tools.jar")
            };
            for (File c : candidates) {
                if (c.isFile()) {
                    return c;
                }
            }
            return null;
        }
    }
}
