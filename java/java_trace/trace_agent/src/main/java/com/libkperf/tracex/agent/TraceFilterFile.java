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
 * Description: Loads and parses the trace_filter.conf file for include/exclude rules and context settings
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
    public boolean includeAll = false;

    public static TraceFilterFile load(String path) {
        TraceFilterFile out = new TraceFilterFile();
        if (path == null || path.length() == 0) {
            return out;
        }
        File f = new File(path);
        if (!f.isFile()) {
            System.err.println("[trace_agent] filter config not found: " + path);
            return out;
        }
        String section = "";
        try (BufferedReader br = new BufferedReader(new InputStreamReader(new FileInputStream(f), StandardCharsets.UTF_8))) {
            String line;
            while ((line = br.readLine()) != null) {
                String s = Util.stripComment(line).trim();
                if (s.length() == 0) {
                    continue;
                }
                if (s.startsWith("[") && s.endsWith("]")) {
                    section = s.substring(1, s.length() - 1).trim().toLowerCase();
                    continue;
                }
                int eq = s.indexOf('=');
                if (eq > 0) {
                    String k = s.substring(0, eq).trim().toLowerCase();
                    String v = s.substring(eq + 1).trim();
                    if ("slot_count".equals(k) || "slotcount".equals(k)) {
                        out.slotCount = Util.parseLong(v, out.slotCount);
                    } else if ("context_depth".equals(k) || "contextdepth".equals(k)) {
                        out.contextDepth = Util.parseInt(v, out.contextDepth);
                    } else if ("context_max_methods".equals(k) || "contextmaxmethods".equals(k)) {
                        out.contextMaxMethods = Util.parseInt(v, out.contextMaxMethods);
                    } else if ("include_all".equals(k) || "includeall".equals(k)) {
                        out.includeAll = "true".equals(v.trim());
                    } else if ("include".equals(k) || "whitelist".equals(k) || "white".equals(k)) {
                        Util.addRules(out.includes, v);
                    } else if ("exclude".equals(k) || "blacklist".equals(k) || "black".equals(k)) {
                        Util.addRules(out.excludes, v);
                    }
                    continue;
                }
                if (s.startsWith("+")) {
                    Util.addRules(out.includes, s.substring(1));
                } else if (s.startsWith("-")) {
                    Util.addRules(out.excludes, s.substring(1));
                } else if ("exclude".equals(section) || "blacklist".equals(section) || "black".equals(section)) {
                    Util.addRules(out.excludes, s);
                } else {
                    Util.addRules(out.includes, s);
                }
            }
        } catch (Exception e) {
            System.err.println("[trace_agent] read filter config failed: " + path + ", " + e);
        }
        return out;
    }
}
