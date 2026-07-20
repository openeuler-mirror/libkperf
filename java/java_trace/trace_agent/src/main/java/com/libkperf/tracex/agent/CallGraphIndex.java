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
 * Description: Builds and indexes the call graph for traced methods, mapping method IDs to their metadata
 ******************************************************************************/
package com.libkperf.tracex.agent;

import com.libkperf.tracex.agent.asm.TraceClassFileTransformer;
import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassVisitor;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Opcodes;

import java.lang.instrument.Instrumentation;
import java.lang.reflect.Modifier;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

public final class CallGraphIndex {

    private final Map<MethodId, Set<MethodId>> callees = new HashMap<MethodId, Set<MethodId>>();
    private final Map<MethodId, Set<MethodId>> callers = new HashMap<MethodId, Set<MethodId>>();
    private final Set<MethodId> methods = new HashSet<MethodId>();

    // owner + name + desc
    public static CallGraphIndex build(Instrumentation inst, TraceClassFileTransformer transformer, TraceConfig config) {
        CallGraphIndex idx = new CallGraphIndex();
        for (Class<?> c : inst.getAllLoadedClasses()) {
            try {
                if (c == null || !inst.isModifiableClass(c)) {
                    continue;
                }
                int m = c.getModifiers();
                if (Modifier.isInterface(m) || c.isAnnotation() || c.isArray() || c.isPrimitive()) {
                    continue;
                }
                String internal = Util.internalName(c);
                if (transformer.isStructuralExcluded(internal)) {
                    continue;
                }
                if (config.isExcludedClass(internal)) {
                    continue;
                }
                byte[] bytes = Util.readClassBytes(c.getClassLoader(), internal);
                if (bytes == null) {
                    continue;
                }
                idx.accept(bytes, config);
            } catch (Throwable t) {
                TraceLog.warn("[trace_agent] skip call graph class " + c + ": " + t, t);
            }
        }
        return idx;
    }

    // expand context based on the specified function
    public Set<MethodId> expandContext(TraceConfig config) {
        if (config.contextDepth <= 0 || config.contextMaxMethods == 0) {
            return Collections.emptySet();
        }
        Set<MethodId> roots = new HashSet<MethodId>();
        for (MethodId id : methods) {
            if (config.matchesUserInclude(id.owner, id.name, id.desc)) roots.add(id);
        }
        if (roots.isEmpty()) {
            return Collections.emptySet();
        }
        Set<MethodId> visited = new HashSet<MethodId>(roots);
        Set<MethodId> expanded = new HashSet<MethodId>();
        ArrayDeque<MethodId> q = new ArrayDeque<MethodId>(roots);
        Map<MethodId, Integer> depth = new HashMap<MethodId, Integer>();
        for (MethodId r : roots) {
            depth.put(r, 0);
        }
        while (!q.isEmpty()) {
            MethodId cur = q.removeFirst();
            int d = depth.get(cur);
            if (d >= config.contextDepth) {
                continue;
            }
            List<MethodId> next = new ArrayList<MethodId>();
            Set<MethodId> cs = callees.get(cur);
            if (cs != null) next.addAll(cs);
            Set<MethodId> rs = callers.get(cur);
            if (rs != null) next.addAll(rs);
            for (MethodId n : next) {
                if (!methods.contains(n)) {
                    continue;
                }
                if (visited.add(n)) {
                    expanded.add(n);
                    if (expanded.size() >= config.contextMaxMethods) {
                        return expanded;
                    }
                    depth.put(n, d + 1);
                    q.addLast(n);
                }
            }
        }
        return expanded;
    }

    private void accept(byte[] bytes, final TraceConfig config) {
        ClassReader cr = new ClassReader(bytes);
        cr.accept(new ClassVisitor(Opcodes.ASM9) {
            String owner;

            @Override
            public void visit(int version, int access, String name, String signature, String superName, String[] interfaces) {
                owner = name;
            }

            @Override
            public MethodVisitor visitMethod(int access, String name, String descriptor, String signature, String[] exceptions) {
                if ((access & (Opcodes.ACC_ABSTRACT | Opcodes.ACC_NATIVE)) != 0) {
                    return null;
                }
                if (config.isExcludedMethod(owner, name, descriptor)) {
                    return null;
                }
                final MethodId id = new MethodId(owner, name, descriptor);
                methods.add(id);
                return new MethodVisitor(Opcodes.ASM9) {
                    @Override
                    public void visitMethodInsn(int opcode, String owner, String name, String descriptor, boolean isInterface) {
                        if (config.isExcludedMethod(owner, name, descriptor)) {
                            return;
                        }
                        MethodId callee = new MethodId(owner, name, descriptor);
                        addEdge(id, callee);
                    }
                };
            }
        }, ClassReader.SKIP_FRAMES);
    }

    private void addEdge(MethodId caller, MethodId callee) {
        Set<MethodId> a = callees.get(caller);
        if (a == null) {
            a = new HashSet<MethodId>(); callees.put(caller, a);
        }
        a.add(callee);
        Set<MethodId> b = callers.get(callee);
        if (b == null) {
            b = new HashSet<MethodId>(); callers.put(callee, b);
        }
        b.add(caller);
    }
}
