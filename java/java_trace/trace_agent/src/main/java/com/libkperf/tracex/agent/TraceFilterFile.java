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
 * Description: Strict trace_filter.conf parser for Java trace include/exclude rules and context settings
 ******************************************************************************/
package com.libkperf.tracex.agent;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

public final class TraceFilterFile {
    public final List<FilterRule> includes = new ArrayList<FilterRule>();
    public final List<FilterRule> excludes = new ArrayList<FilterRule>();
    public long slotCount = -1;
    public int contextDepth = -1;
    public int contextMaxMethods = -1;
    public boolean valid = false;

    public static TraceFilterFile load(String path) {
        TraceFilterFile out = new TraceFilterFile();
        if (path == null || path.length() == 0) {
            TraceLog.info("[trace_agent] filter config path is empty");
            return out;
        }
        File f = new File(path);
        if (!f.isFile()) {
            TraceLog.info("[trace_agent] filter config not found: " + path);
            return out;
        }

        String section = "";
        out.valid = true;

        try (BufferedReader br = new BufferedReader(new InputStreamReader(new FileInputStream(f),
                StandardCharsets.UTF_8))) {
            String line;
            int lineNo = 0;
            while ((line = br.readLine()) != null) {
                lineNo++;
                String s = Util.stripComment(line).trim();
                if (s.length() == 0) {
                    continue;
                }
                if (s.startsWith("[") || s.endsWith("]")) {
                    if (!(s.startsWith("[") && s.endsWith("]") && s.length() > 2)) {
                        return invalid(out, path, lineNo, "invalid section", s);
                    }
                    section = s.substring(1, s.length() - 1).trim();
                    if ("java_include".equals(section)) {
                        continue;
                    } else if ("java_exclude".equals(section)) {
                        continue;
                    }
                    return invalid(out, path, lineNo, "unknown section", s);
                }

                int eq = s.indexOf('=');
                if (eq >= 0) {
                    if (section.length() != 0) {
                        return invalid(out, path, lineNo, "key/value is not allowed inside section", s);
                    }
                    String k = s.substring(0, eq).trim();
                    String v = s.substring(eq + 1).trim();
                    if ("slot_count".equals(k)) {
                        Long parsed = parsePositiveLong(v);
                        if (parsed == null) {
                            return invalid(out, path, lineNo, "invalid slot_count", v);
                        }
                        out.slotCount = parsed.longValue();
                    } else if ("context_depth".equals(k)) {
                        Integer parsed = parseNonNegativeInt(v);
                        if (parsed == null) {
                            return invalid(out, path, lineNo, "invalid context_depth", v);
                        }
                        out.contextDepth = parsed.intValue();
                    } else if ("context_max_methods".equals(k)) {
                        Integer parsed = parseNonNegativeInt(v);
                        if (parsed == null) {
                            return invalid(out, path, lineNo, "invalid context_max_methods", v);
                        }
                        out.contextMaxMethods = parsed.intValue();
                    } else {
                        return invalid(out, path, lineNo, "unknown key", k);
                    }
                    continue;
                }

                if (!("java_include".equals(section) || "java_exclude".equals(section))) {
                    return invalid(out, path, lineNo, "rule must be inside java section", s);
                }
                if (!isStrictRule(s)) {
                    return invalid(out, path, lineNo, "invalid rule", s);
                }
                if ("java_exclude".equals(section)) {
                    Util.addRules(out.excludes, s);
                } else {
                    Util.addRules(out.includes, s);
                }
            }
        } catch (Exception e) {
            out.valid = false;
            TraceLog.warn("[trace_agent] read filter config failed: " + path + ", " + e, e);
            return out;
        }

        return out;
    }

    private static TraceFilterFile invalid(TraceFilterFile out, String path, int lineNo, String reason, String value) {
        out.valid = false;
        TraceLog.info("[trace_agent] invalid trace filter config: " + path + ":" + lineNo
                + ", " + reason + ": " + value);
        return out;
    }

    private static boolean isStrictRule(String value) {
        if (value == null || value.length() == 0) {
            return false;
        }
        if (value.charAt(0) == '+' || value.charAt(0) == '-' || value.indexOf(',') >= 0
                || value.indexOf('=') >= 0 || value.indexOf('#') >= 0 || value.indexOf('!') >= 0
                || containsWhitespace(value)) {
            return false;
        }
        int explicit = value.indexOf("::");
        if (explicit >= 0) {
            return explicit > 0 && explicit + 2 < value.length() && value.indexOf("::", explicit + 2) < 0;
        }
        if (value.indexOf('(') >= 0 || value.indexOf(')') >= 0) {
            return false;
        }
        return !looksLikeImplicitMethodRule(value, '.');
    }

    private static boolean looksLikeImplicitMethodRule(String value, char sep) {
        int p = value.lastIndexOf(sep);
        if (p <= 0 || p + 1 >= value.length()) {
            return false;
        }
        String owner = value.substring(0, p);
        String tail = value.substring(p + 1);
        int prevSep = owner.lastIndexOf(sep);
        String prev = prevSep < 0 ? owner : owner.substring(prevSep + 1);
        if ("*".equals(tail) || "**".equals(tail)) {
            return startsUpper(prev);
        }
        return startsUpper(prev) && startsLower(tail);
    }

    private static boolean startsUpper(String value) {
        return value != null && value.length() > 0 && Character.isUpperCase(value.charAt(0));
    }

    private static boolean startsLower(String value) {
        return value != null && value.length() > 0 && Character.isLowerCase(value.charAt(0));
    }

    private static boolean containsWhitespace(String value) {
        for (int i = 0; i < value.length(); i++) {
            if (Character.isWhitespace(value.charAt(i))) {
                return true;
            }
        }
        return false;
    }

    private static Long parsePositiveLong(String value) {
        if (value == null || value.length() == 0) {
            return null;
        }
        for (int i = 0; i < value.length(); i++) {
            if (!Character.isDigit(value.charAt(i))) {
                return null;
            }
        }
        try {
            long parsed = Long.parseLong(value);
            return parsed > 0L ? Long.valueOf(parsed) : null;
        } catch (NumberFormatException e) {
            return null;
        }
    }

    private static Integer parseNonNegativeInt(String value) {
        Long parsed = parsePositiveLong(value);
        if (parsed == null) {
            return "0".equals(value) ? Integer.valueOf(0) : null;
        }
        return parsed.longValue() <= Integer.MAX_VALUE ? Integer.valueOf(parsed.intValue()) : null;
    }
}
