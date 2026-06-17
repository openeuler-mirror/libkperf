/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * libkperf licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: Wu
 * Create: 2026-06-11
 * Description: Shared helpers for Java trace agent parsing, class names, and class/resource bytes
 ******************************************************************************/
package com.libkperf.tracex.agent;

import java.io.ByteArrayOutputStream;
import java.io.FileInputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.BufferedReader;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.Map;

public final class Util {
    private Util() {}

    public static String get(Map<String, String> m, String k, String d) {
        String v = m.get(k);
        return v == null ? d : v;
    }

    public static int parseInt(String v, int fallback) {
        try {
            return Integer.parseInt(v.trim());
        } catch (Exception ignored) {
            return fallback;
        }
    }

    public static long parseLong(String v, long fallback) {
        try {
            return Long.parseLong(v.trim());
        } catch (Exception ignored) {
            return fallback;
        }
    }

    public static String stripComment(String line) {
        int hash = line.indexOf('#');
        int slashes = line.indexOf("//");
        int end = line.length();
        if (hash >= 0) {
            end = Math.min(end, hash);
        }
        if (slashes >= 0) {
            end = Math.min(end, slashes);
        }
        return line.substring(0, end);
    }

    public static void addRules(List<FilterRule> target, String text) {
        if (text == null || text.length() == 0) {
            return;
        }
        for (String p : text.split("[,\\n\\r]")) {
            FilterRule r = FilterRule.parse(p);
            if (r != null) {
                target.add(r);
            }
        }
    }

    public static String readTextFile(String path, String errorPrefix) {
        if (path == null || path.length() == 0) {
            return "";
        }
        StringBuilder sb = new StringBuilder();
        try (BufferedReader br = new BufferedReader(
                new InputStreamReader(new FileInputStream(path), StandardCharsets.UTF_8))) {
            String line;
            while ((line = br.readLine()) != null) sb.append(line).append('\n');
        } catch (Exception e) {
            System.err.println(errorPrefix + path + ", " + e);
        }
        return sb.toString();
    }

    public static String safeClassName(Class<?> c) {
        if (c == null) {
            return "null";
        }
        try {
            return c.getName();
        } catch (Throwable t) {
            return String.valueOf(c);
        }
    }

    public static String internalName(Class<?> c) {
        return c.getName().replace('.', '/');
    }

    public static byte[] readClassBytes(ClassLoader loader, String internalName) {
        if (internalName == null) {
            return null;
        }
        try {
            InputStream in = loader == null
                    ? ClassLoader.getSystemResourceAsStream(internalName + ".class")
                    : loader.getResourceAsStream(internalName + ".class");
            if (in == null) {
                return null;
            }
            try {
                ByteArrayOutputStream bos = new ByteArrayOutputStream();
                byte[] buf = new byte[8192];
                int n;
                while ((n = in.read(buf)) >= 0) {
                    bos.write(buf, 0, n);
                }
                return bos.toByteArray();
            } finally {
                try { in.close(); } catch (Exception ignored) {}
            }
        } catch (Throwable t) {
            return null;
        }
    }
}
