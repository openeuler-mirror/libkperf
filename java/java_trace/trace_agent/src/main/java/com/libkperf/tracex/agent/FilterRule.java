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
 * Description: Represents a single include/exclude filter rule with wildcard pattern matching for class and method names
 ******************************************************************************/
package com.libkperf.tracex.agent;

import java.util.regex.Pattern;

public final class FilterRule {
    private final String classPattern;
    private final String methodPattern;
    private final String descPattern;
    private final Pattern classRegex;
    private final Pattern methodRegex;
    private final Pattern descRegex;

    private FilterRule(String classPattern, String methodPattern, String descPattern) {
        this.classPattern = normalizeClass(classPattern);
        this.methodPattern = normalizeMethod(methodPattern);
        this.descPattern = normalizeDescriptor(descPattern);
        this.classRegex = compileClassWildcardRegex(this.classPattern);
        this.methodRegex = compileSimpleWildcardRegex(this.methodPattern);
        this.descRegex = compileSimpleWildcardRegex(this.descPattern);
    }

    public static FilterRule parse(String raw) {
        if (raw == null) {
            return null;
        }
        String s = raw.trim();
        if (s.length() == 0 || s.startsWith("#") || s.startsWith("//") || s.startsWith("[")) {
            return null;
        }
        if (s.charAt(0) == '+' || s.charAt(0) == '-') {
            s = s.substring(1).trim();
        }
        if (s.length() == 0) {
            return null;
        }

        String cls = s;
        String method = "*";
        String desc = "";

        Split explicit = splitExplicit(s);
        if (explicit != null) {
            cls = explicit.owner;
            method = explicit.method;
        } else {
            Split implicit = splitImplicitClassMethod(s);
            if (implicit != null) {
                cls = implicit.owner;
                method = implicit.method;
            }
        }

        int descStart = method.indexOf('(');
        if (descStart >= 0) {
            desc = method.substring(descStart);
            method = method.substring(0, descStart);
        }
        if (method.length() == 0) {
            method = "*";
        }
        return new FilterRule(cls, method, desc);
    }

    public boolean matchesClass(String classNameInternal) {
        String name = normalizeClass(classNameInternal);

        if (classPattern.length() == 0 || "*".equals(classPattern) || "**".equals(classPattern)) {
            return true;
        }
        if (classRegex != null) {
            return classRegex.matcher(name).matches();
        }
        return name.equals(classPattern);
    }

    public boolean matchesMethod(String owner, String name, String desc) {
        if (!matchesClass(owner)) {
            return false;
        }
        String m = name == null ? "" : name;
        String d = desc == null ? "" : desc;

        boolean methodOk = methodPattern.length() == 0
                || "*".equals(methodPattern)
                || "**".equals(methodPattern)
                || (methodRegex == null ? methodPattern.equals(m) : methodRegex.matcher(m).matches());

        boolean descOk = descPattern.length() == 0
                || "*".equals(descPattern)
                || "**".equals(descPattern)
                || (descRegex == null ? descPattern.equals(d) : descRegex.matcher(d).matches());

        return methodOk && descOk;
    }

    public boolean isClassOnly() {
        return (methodPattern.length() == 0 || "*".equals(methodPattern) || "**".equals(methodPattern))
                && descPattern.length() == 0;
    }

    @Override
    public String toString() {
        if (isClassOnly()) {
            return classPattern;
        }
        return classPattern + "::" + methodPattern + descPattern;
    }

    private static Split splitExplicit(String s) {
        int sep = s.indexOf("::");
        if (sep >= 0) {
            return new Split(s.substring(0, sep), s.substring(sep + 2));
        }
        sep = s.indexOf('#');
        if (sep > 0) {
            return new Split(s.substring(0, sep), s.substring(sep + 1));
        }
        sep = s.indexOf('!');
        if (sep > 0) {
            return new Split(s.substring(0, sep), s.substring(sep + 1));
        }
        return null;
    }

    private static Split splitImplicitClassMethod(String s) {
        Split slash = splitByLastSeparator(s, '/');
        if (slash != null) {
            return slash;
        }
        return splitByLastSeparator(s, '.');
    }

    private static Split splitByLastSeparator(String s, char sep) {
        int p = s.lastIndexOf(sep);
        if (p <= 0 || p + 1 >= s.length()) {
            return null;
        }
        String owner = s.substring(0, p);
        String tail = s.substring(p + 1);
        if (!looksLikeMethodTail(owner, tail, sep)) {
            return null;
        }
        return new Split(owner, tail);
    }

    private static boolean looksLikeMethodTail(String owner, String tail, char sep) {
        if (tail == null || tail.length() == 0) {
            return false;
        }
        if (tail.indexOf('(') >= 0 || tail.startsWith("<")) {
            return true;
        }
        String prev = previousSegment(owner, sep);
        if ("*".equals(tail) || "**".equals(tail)) {
            return startsUpper(prev);
        }
        if (startsUpper(prev)) {
            return true;
        }
        return false;
    }

    private static String previousSegment(String s, char sep) {
        int p = s.lastIndexOf(sep);
        return p < 0 ? s : s.substring(p + 1);
    }

    private static boolean startsUpper(String s) {
        return s != null && s.length() > 0 && Character.isUpperCase(s.charAt(0));
    }

    private static String normalizeClass(String value) {
        String s = value == null ? "" : value.trim();
        if (s.startsWith("L") && s.endsWith(";")) {
            s = s.substring(1, s.length() - 1);
        }
        while (s.endsWith("/")) {
            s = s.substring(0, s.length() - 1);
        }
        return s.replace('.', '/');
    }

    private static String normalizeMethod(String value) {
        String s = value == null ? "*" : value.trim();
        return s.length() == 0 ? "*" : s;
    }

    private static String normalizeDescriptor(String value) {
        return value == null ? "" : value.trim();
    }

    private static Pattern compileClassWildcardRegex(String pattern) {
        if (pattern == null || pattern.indexOf('*') < 0) {
            return null;
        }
        StringBuilder sb = new StringBuilder();
        sb.append('^');
        for (int i = 0; i < pattern.length(); i++) {
            char c = pattern.charAt(i);
            if (c == '*') {
                boolean doubleStar = i + 1 < pattern.length() && pattern.charAt(i + 1) == '*';
                if (doubleStar) {
                    sb.append(".*");
                    i++;
                } else {
                    sb.append("[^/]*");
                }
                continue;
            }
            appendEscapedRegexChar(sb, c);
        }
        sb.append('$');
        return Pattern.compile(sb.toString());
    }

    private static Pattern compileSimpleWildcardRegex(String pattern) {
        if (pattern == null || pattern.indexOf('*') < 0) {
            return null;
        }

        StringBuilder sb = new StringBuilder();
        sb.append('^');

        for (int i = 0; i < pattern.length(); i++) {
            char c = pattern.charAt(i);
            if (c == '*') {
                while (i + 1 < pattern.length() && pattern.charAt(i + 1) == '*') {
                    i++;
                }
                sb.append(".*");
                continue;
            }
            appendEscapedRegexChar(sb, c);
        }
        sb.append('$');
        return Pattern.compile(sb.toString());
    }

    private static void appendEscapedRegexChar(StringBuilder sb, char c) {
        if ("\\.[]{}()+-^$?|".indexOf(c) >= 0) {
            sb.append('\\');
        }
        sb.append(c);
    }

    private static final class Split {
        final String owner;
        final String method;
        Split(String owner, String method) {
            this.owner = owner;
            this.method = method;
        }
    }
}
