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
 * Create: 2026-07-08
 * Description: Java trace log configuration
 ******************************************************************************/
package com.libkperf.tracex.agent;

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;

public final class TraceLog {
    private static volatile String logFile;

    private TraceLog() {}

    public static void init(String path) {
        if (path == null || path.length() == 0) {
            return;
        }
        logFile = path;
        File parent = new File(path).getParentFile();
        if (parent != null) {
            parent.mkdirs();
        }
    }

    public static void info(String message) {
        write(message, null);
    }

    public static void infoBatch(Iterable<String> messages) {
        if (messages == null) {
            return;
        }
        writeBatch(messages);
    }

    public static void warn(String message, Throwable t) {
        write(message, t);
    }

    private static synchronized void write(String message, Throwable t) {
        String path = logFile;
        if (path == null || path.length() == 0) {
            return;
        }
        try (PrintWriter out = new PrintWriter(
            new OutputStreamWriter(new FileOutputStream(path, true), StandardCharsets.UTF_8))) {
            printEntry(out, new SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS"), message, t);
        } catch (Throwable ignored) {
        }
    }

    private static synchronized void writeBatch(Iterable<String> messages) {
        String path = logFile;
        if (path == null || path.length() == 0) {
            return;
        }
        try (PrintWriter out = new PrintWriter(
            new OutputStreamWriter(new FileOutputStream(path, true), StandardCharsets.UTF_8))) {
            SimpleDateFormat dateFormat = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS");
            for (String message : messages) {
                if (message != null) {
                    printEntry(out, dateFormat, message, null);
                }
            }
        } catch (Throwable ignored) {
        }
    }

    private static void printEntry(PrintWriter out, SimpleDateFormat dateFormat,
                                   String message, Throwable t) {
        out.print('[');
        out.print(dateFormat.format(new Date()));
        out.print("] ");
        out.println(message);
        if (t != null) {
            t.printStackTrace(out);
        }
    }
}
