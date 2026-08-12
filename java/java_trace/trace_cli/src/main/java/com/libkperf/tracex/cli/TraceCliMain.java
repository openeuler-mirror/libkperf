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
import java.io.IOException;
import java.lang.reflect.InvocationTargetException;
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

    private static void validatePid(String pid) {
        for (int i = 0; i < pid.length(); i++) {
            char c = pid.charAt(i);
            if (c < '0' || c > '9') {
                throw new IllegalArgumentException("pid must be numeric: " + pid);
            }
        }
    }

    // agent args format: key=value;key=value
    private static void appendArg(StringBuilder sb, String key, String value) {
        if (value == null || value.length() == 0) {
            return;
        }
        if (sb.length() > 0) {
            sb.append(';');
        }
        sb.append(key).append('=').append(value.replace("\\", "\\\\").replace(";", "\\;").replace("=", "\\="));
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
        validatePid(pid);
        String agentJar = required(opts, "--agent-jar");
        String action = opts.containsKey("--action") ? opts.get("--action") : "start";
        StringBuilder agentArgs = new StringBuilder();
        appendArg(agentArgs, "action", action);
        appendArg(agentArgs, "logFile", opts.get("--log-file"));
        if ("start".equals(action)) {
            appendArg(agentArgs, "shmPath", opts.get("--shm-path"));
            appendArg(agentArgs, "nativeLibPath", opts.get("--native-lib"));
            appendArg(agentArgs, "includeFile", opts.get("--include-file"));
            appendArg(agentArgs, "configFile", opts.get("--config-file"));
        }

        VirtualMachineAccess vmAccess = VirtualMachineAccess.load();
        attachAndLoad(vmAccess, pid, agentJar, agentArgs.toString());
    }

    private static void attachAndLoad(VirtualMachineAccess vmAccess, String pid, String agentJar, String agentArgs) throws Exception {
        Object vm = null;
        try {
            vm = vmAccess.attach(pid);
            vmAccess.loadAgent(vm, agentJar, agentArgs);
        } finally {
            if (vm != null) {
                try {
                    vmAccess.detach(vm);
                } catch (Throwable ignored) {
                }
            }
        }
    }

    private static Throwable rootCause(Throwable t) {
        Throwable cur = t;
        while (cur instanceof InvocationTargetException && ((InvocationTargetException) cur).getTargetException() != null) {
            cur = ((InvocationTargetException) cur).getTargetException();
        }
        while (cur.getCause() != null && cur.getCause() != cur) {
            cur = cur.getCause();
        }
        return cur;
    }

    private static boolean isNonNumericAttachReply(Throwable t) {
        return t instanceof IOException && t.getMessage() != null && t.getMessage().contains("Non-numeric value found");
    }

    // load through reflection, which ensures compatibility of JDK 8 and JDK 9+
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
                // continue try JDK 8
            }

            // JDK8 common case when running with plain java -jar: attach classes live in tools.jar.
            File toolsJar = findToolsJar();
            if (toolsJar != null) {
                ClassLoader loader = new URLClassLoader(new URL[] { toolsJar.toURI().toURL() }, TraceCliMain.class.getClassLoader());
                return new VirtualMachineAccess(Class.forName("com.sun.tools.attach.VirtualMachine", true, loader));
            }
            throw new ClassNotFoundException("JVM attach API not found");
        }

        private Object attach(String pid) throws Exception {
            return attach.invoke(null, pid);
        }

        private void loadAgent(Object vm, String agentJar, String args) throws Exception {
            try {
                loadAgent.invoke(vm, agentJar, args == null ? "" : args);
            } catch (InvocationTargetException e) {
                if (isNonNumericAttachReply(rootCause(e))) {
                    return;
                }
                throw e;
            }
        }

        private void detach(Object vm) throws Exception {
            detach.invoke(vm);
        }

        private static File findToolsJar() {
            String javaHome = System.getProperty("java.home");
            File[] candidates = new File[] {
                new File(javaHome, "lib/tools.jar"),
                new File(javaHome, "../lib/tools.jar")
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
