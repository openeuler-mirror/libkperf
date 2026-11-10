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
 * Create: 2026-07-08
 * Description: Java trace log configuration
 ******************************************************************************/
package com.libkperf.tracex.runtime;

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.FileAlreadyExistsException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.attribute.PosixFilePermission;
import java.nio.file.attribute.PosixFilePermissions;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.EnumSet;
import java.util.Set;

public final class TraceLog {
    private static final Set<PosixFilePermission> PRIVATE_FILE_PERMISSIONS =
        EnumSet.of(PosixFilePermission.OWNER_READ, PosixFilePermission.OWNER_WRITE);
    private static volatile String logFile;

    // Severity thresholds. Higher numeric value means more verbose. A message is
    // emitted only when its level is <= the current threshold. Default INFO keeps
    // debug diagnostics quiet until the user opts in via trace_filter.conf.
    private static final int LEVEL_ERROR = 0;
    private static final int LEVEL_WARNING = 1;
    private static final int LEVEL_INFO = 2;
    private static final int LEVEL_DEBUG = 3;
    private static volatile int level = LEVEL_INFO;

    private TraceLog() {}

    public static void init(String path) {
        if (path == null || path.length() == 0) {
            return;
        }
        File parent = new File(path).getParentFile();
        if (parent != null) {
            parent.mkdirs();
        }
        if (ensurePrivateLogFile(path)) {
            logFile = path;
        }
    }

    // Apply the TraceLog threshold from a trace_filter.conf log_level value
    // ("error", "warning", "info", "debug"). Case-insensitive; unknown values
    // fall back to INFO. Safe to call before/after init.
    public static void setLevelByName(String name) {
        if (name == null) {
            return;
        }
        String v = name.trim().toLowerCase();
        if ("error".equals(v)) {
            level = LEVEL_ERROR;
        } else if ("warning".equals(v) || "warn".equals(v)) {
            level = LEVEL_WARNING;
        } else if ("debug".equals(v)) {
            level = LEVEL_DEBUG;
        } else {
            level = LEVEL_INFO;
        }
    }

    private static boolean ensurePrivateLogFile(String path) {
        Path logPath = Paths.get(path);
        try {
            try {
                Files.createFile(logPath, PosixFilePermissions.asFileAttribute(PRIVATE_FILE_PERMISSIONS));
            } catch (FileAlreadyExistsException ignored) {
            }
            Files.setPosixFilePermissions(logPath, PRIVATE_FILE_PERMISSIONS);
            return true;
        } catch (Throwable ignored) {
            return false;
        }
    }

    public static void error(String message) {
        write(LEVEL_ERROR, message, null);
    }

    public static void warn(String message, Throwable t) {
        write(LEVEL_WARNING, message, t);
    }

    public static void warn(String message) {
        write(LEVEL_WARNING, message, null);
    }

    public static void info(String message) {
        write(LEVEL_INFO, message, null);
    }

    public static void debug(String message) {
        write(LEVEL_DEBUG, message, null);
    }

    public static boolean isDebugEnabled() {
        return level >= LEVEL_DEBUG;
    }

    public static void debug(String message, Throwable t) {
        write(LEVEL_DEBUG, message, t);
    }

    private static synchronized void write(int messageLevel, String message, Throwable t) {
        if (messageLevel > level) {
            return;
        }
        String path = logFile;
        if (path == null) {
            return;
        }
        try (PrintWriter out = new PrintWriter(
            new OutputStreamWriter(new FileOutputStream(path, true), StandardCharsets.UTF_8))) {
            out.print('[');
            out.print(new SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS").format(new Date()));
            out.print("] ");
            out.println(message);
            if (t != null) {
                t.printStackTrace(out);
            }
        } catch (Throwable ignored) {
        }
    }
}
