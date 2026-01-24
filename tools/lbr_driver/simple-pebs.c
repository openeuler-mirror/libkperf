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
 * Create: 2026-01-23
 * Description: Provides the capability of PEBS LBR collection
===============================================================================
 * This code is based on the original work by Intel Corporation, 
 * which is licensed under the 2-Clause BSD License as follows:
 *
 * Copyright (c) 2015, Intel Corporation
 * Author: Andi Kleen
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Alternatively this code can be used under the terms of the GPLv2.
 *
 * Original code also offered under GPLv2, but this derivative work
 * elects to use the BSD-style terms to maximize compatibility.
 ******************************************************************************/

#define DEBUG 1
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/notifier.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/poll.h>
#include <linux/percpu.h>
#include <linux/wait.h>
#include <linux/kallsyms.h>
#include <linux/version.h>
#include <asm/msr.h>
#include <asm/desc.h>
#include <cpuid.h>

#include "simple-pebs.h"

#define MSR_IA32_PERFCTR0      0x000000c1
#define MSR_IA32_EVNTSEL0      0x00000186

#define MSR_IA32_PERF_CABABILITIES   0x00000345
#define MSR_IA32_PERF_GLOBAL_STATUS   0x0000038e
#define MSR_IA32_PERF_GLOBAL_CTRL  0x0000038f
#define MSR_IA32_PERF_GLOBAL_OVF_CTRL  0x00000390
#define MSR_IA32_PEBS_ENABLE    0x000003f1
#define MSR_IA32_DS_AREA      0x00000600

#define EVTSEL_USR BIT(16)
#define EVTSEL_OS BIT(17)
#define EVTSEL_INT BIT(20)
#define EVTSEL_EN BIT(22)

#define S_PEBS_BUFFER_SIZE (64 * 1024) /* PEBS buffer size */
#define OUT_BUFFER_SIZE   (64 * 1024) /* must be multiple of 4k */
#define PERIOD 100000  /* configure: CPU freq / freq */

#define FEAT1_PDCM BIT(15)
#define FEAT2_DS  BIT(21)

/* Deal with Gleixnerfication */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,8,0)

/* No CPU hotplug / suspend with the mess in newer kernels. */

static inline void register_cpu_notifier(struct notifier_block *n) {}
static inline void unregister_cpu_notifier(struct notifier_block *n) {}

#define CPU_STARTING 0
#define CPU_DYING 1

#endif

static unsigned pebs_event; 
static volatile int pebs_error;
static int pebs_vector = 0xf0;
static bool pebs_baseline;

static int lbr_depth = 32;
module_param(lbr_depth, int, 0444);
MODULE_PARM_DESC(lbr_depth, "PEBS LBR entries (1..32)");

struct s_debug_store {
    u64 bts_base;
    u64 bts_index;
    u64 bts_max;
    u64 bts_thresh;

    u64 pebs_base;
    u64 pebs_index;
    u64 pebs_max;
    u64 pebs_thresh;
    u64 pebs_reset[4];
};

static DEFINE_PER_CPU(void *, out_buffer_base);
static DEFINE_PER_CPU(void *, out_buffer);
static DEFINE_PER_CPU(void *, out_buffer_end);

static DEFINE_PER_CPU(struct s_debug_store *, cpu_ds);
static DEFINE_PER_CPU(unsigned long, cpu_old_ds);

static DEFINE_PER_CPU(wait_queue_head_t, simple_pebs_wait);

static DEFINE_PER_CPU(u64, old_lvtpc);
static DEFINE_PER_CPU(int, cpu_initialized);

static bool check_cpu(void)
{
    unsigned a, b, c, d;
    unsigned max, model, fam;
    unsigned feat1, feat2;
    
    __cpuid(0, max, b, c, d);
    if (memcmp(&b, "Genu", 4)) {
        pr_err("Not an Intel CPU\n");
        return false;
    }

    __cpuid(1, a, b, feat1, feat2);
    model = ((a >> 4) & 0xf);
    fam = (a >> 8) & 0xf;
    if (fam == 6 || fam == 0xf)
        model += ((a >> 16) & 0xf) << 4;
    if (fam != 6) {
        pr_err("Not an supported Intel CPU\n");
        return false;
    }
    
    switch (model) { 
    case 58: /* IvyBridge */
    case 63: /* Haswell_EP */
    case 69: /* Haswell_ULT */
    case 61: /* Broadwell client */
    case 78: /* Skylake */
    case 85: /* Skylake */
    case 86: /* Broadwell */
    case 94: /* Skylake */
    case 142: /* Kabylake */
    case 158: /* Kabylake */
    case 207:
        /* pebs_event = 0x1c2; for UOPS_RETIRED.ALL */
        pebs_event = 0x003c; /*cycles*/
        break;

    case 55: /* Bay Trail */
    case 76: /* Airmont */
    case 77: /* Avoton */
        pebs_event = 0x0c5; /* BR_MISP_RETIRED.ALL_BRANCHES */
        break;

    default:
        pr_err("Unknown CPU model %d\n", model);
        return false;
    }

    /* Check if we support arch perfmon */
    if (max >= 0xa) {
        __cpuid(0xa, a, b, c, d);
        if ((a & 0xff) < 1) { 
            pr_err("No arch perfmon support\n");
            return false;
        }
        if (((a >> 8) & 0xff) < 1) { 
            pr_err("No generic counters\n");
            return false;
        }
    } else {
        pr_err("No arch perfmon support\n");
        return false;
    }
        
    /* check if we support DS */
    if (!(feat2 & FEAT2_DS)) {
        pr_err("No debug store support\n");
        return false;
    }

    /* check perf capability */
    if (feat1 & FEAT1_PDCM) {
        u64 cap;
        int format;
        rdmsrl(MSR_IA32_PERF_CAPABILITIES, cap);
        format = (cap >> 8) & 0xf;
        switch (format) {
        case 1:
        case 2:
        case 3:
            pr_info("PEBS (v%d) detected. Adaptive PEBS of LBR collection is not supported\n", format);
            return false;
        case 4:
        case 5:
            pr_info("Adaptive PEBS (v%d) detected\n", format);
            break;
        default:
            pr_err("Unsupported PEBS format %d\n",format);
            return false;
        }
        pebs_baseline = !!(cap & KPERF_CAP_PEBS_BASELINE) && (format >= 4);
        pr_info("PERF_CAP=%llx pebs_format=%d baseline=%d\n", cap, format, pebs_baseline);
        /* Could check PEBS_TRAP */
    } else {
        pr_err("No PERF_CAPABILITIES support\n");
        return false;
    }

    return true;
}

static int simple_pebs_mmap(struct file *file, struct vm_area_struct *vma)
{
    unsigned long len = vma->vm_end - vma->vm_start;
    int cpu = (int)(long)file->private_data;

    if (len % PAGE_SIZE || len != OUT_BUFFER_SIZE || vma->vm_pgoff)
        return -EINVAL;

    if (vma->vm_flags & VM_WRITE)
        return -EPERM;

    if (!cpu_online(cpu))
        return -EIO;

    return remap_pfn_range(vma, vma->vm_start,
                __pa(per_cpu(out_buffer_base, cpu)) >> PAGE_SHIFT,
                OUT_BUFFER_SIZE,
                vma->vm_page_prot);
}

static unsigned int simple_pebs_poll(struct file *file, poll_table *wait)
{
    unsigned long cpu = (unsigned long)file->private_data;
    poll_wait(file, &per_cpu(simple_pebs_wait, cpu), wait);
    if (per_cpu(out_buffer, cpu) > per_cpu(out_buffer_base, cpu))
        return POLLIN | POLLRDNORM;
    return 0;
}

static void status_dump(char *where)
{
#if 1
    u64 val, val2;
    rdmsrl(MSR_IA32_PERF_GLOBAL_STATUS, val);
    rdmsrl(MSR_IA32_PERF_GLOBAL_CTRL, val2);
    pr_debug("%d: %s: status %llx ctrl %llx counter %llx\n", smp_processor_id(), where, val, val2, __builtin_ia32_rdpmc(0));
#endif
}

static void start_stop_cpu(void *arg)
{
    wrmsrl(MSR_IA32_PERF_GLOBAL_CTRL, arg ? 1 : 0);
    status_dump("stop");
}

static void reset_buffer_cpu(void *arg)
{
    struct s_debug_store *ds = __this_cpu_read(cpu_ds);

    wrmsrl(MSR_IA32_PERF_GLOBAL_CTRL, 0);

    /* reset pebs_index, out_buffer, adn reinstall the counter */
    if (ds) {
        ds->pebs_index = ds->pebs_base;
    }
    __this_cpu_write(out_buffer, __this_cpu_read(out_buffer_base));
    wrmsrl(MSR_IA32_PERFCTR0, -(long long)PERIOD);
    wrmsrl(MSR_IA32_PERF_GLOBAL_OVF_CTRL,
           KPERF_GLOBAL_OVF_BUF | KPERF_GLOBAL_LBR_FRZ | KPERF_GLOBAL_OVF_PMC0);

    wrmsrl(MSR_IA32_PERF_GLOBAL_CTRL, 1);
}

static DEFINE_MUTEX(reset_mutex);

static long simple_pebs_ioctl(struct file *file, unsigned int cmd,
                        unsigned long arg)
{
    unsigned long cpu = (unsigned long)file->private_data;

    switch (cmd) {
    case SIMPLE_PEBS_SET_CPU:
        cpu = arg;
        if (cpu >= NR_CPUS || !cpu_online(cpu))
            return -EINVAL;
        file->private_data = (void *)cpu;
        return 0;
    case SIMPLE_PEBS_GET_SIZE:
        return put_user(OUT_BUFFER_SIZE, (int *)arg);
    case SIMPLE_PEBS_GET_OFFSET: {
        unsigned len = per_cpu(out_buffer, cpu) - per_cpu(out_buffer_base, cpu);
        return put_user(len, (unsigned *)arg);
    }
    case SIMPLE_PEBS_START:
        /* reset out_buffer and pebs_index to get new data */
        mutex_lock(&reset_mutex);
        smp_call_function_single(cpu, reset_buffer_cpu, NULL, 1);
        smp_call_function_single(cpu, start_stop_cpu, (void*)1, 1);
        mutex_unlock(&reset_mutex);
        return 0;
    case SIMPLE_PEBS_STOP:
        mutex_lock(&reset_mutex);
        smp_call_function_single(cpu, start_stop_cpu,
                (void *)(long)(cmd == SIMPLE_PEBS_START), 1);
        mutex_unlock(&reset_mutex);
        return 0;
    case SIMPLE_PEBS_RESET:
        mutex_lock(&reset_mutex);
        smp_call_function_single(cpu, reset_buffer_cpu, NULL, 1);
        mutex_unlock(&reset_mutex);
        return 0;
    default:
        return -ENOTTY;
    }
}

static const struct file_operations simple_pebs_fops = {
    .owner = THIS_MODULE,
    .mmap = simple_pebs_mmap,
    .poll = simple_pebs_poll,
    .unlocked_ioctl = simple_pebs_ioctl,
    .llseek = noop_llseek,
};

static struct miscdevice simple_pebs_miscdev = {
    MISC_DYNAMIC_MINOR,
    "simple-pebs",
    &simple_pebs_fops
};

/* Allocate DS and PEBS buffer */
static int allocate_buffer(void)
{
    struct s_debug_store *ds;

    ds = kmalloc(sizeof(struct s_debug_store), GFP_KERNEL);
    if (!ds) {
        pr_err("Cannot allocate DS\n");
        return -ENOMEM;
    }
    memset(ds, 0, sizeof(*ds));

    ds->pebs_base = (unsigned long)kmalloc(S_PEBS_BUFFER_SIZE, GFP_KERNEL);
    if (!ds->pebs_base) {
        pr_err("Cannot allocate PEBS buffer\n");
        kfree(ds);
        return -ENOMEM;
    }

    memset((void *)ds->pebs_base, 0, S_PEBS_BUFFER_SIZE);

    ds->pebs_index = ds->pebs_base;
    ds->pebs_max = ds->pebs_base + S_PEBS_BUFFER_SIZE;
    ds->pebs_thresh = ds->pebs_base + (S_PEBS_BUFFER_SIZE * 9) / 10;

    /* Counter reset value */
    ds->pebs_reset[0] = -(long long)PERIOD;

    __this_cpu_write(cpu_ds, ds);

    status_dump("allocate_buffer");
    return 0;
}

static int allocate_out_buf(void)
{
    unsigned long addr;
    /* for physical continuous memory */
    addr = __get_free_pages(GFP_KERNEL | __GFP_ZERO, get_order(OUT_BUFFER_SIZE));
    if (!addr) {
        pr_err("Cannot allocate out buffer\n");
        return -ENOMEM;
    }

    __this_cpu_write(out_buffer_base, (void *)addr);
    __this_cpu_write(out_buffer, (void *)addr);
    __this_cpu_write(out_buffer_end, (void *)(addr + OUT_BUFFER_SIZE));
    return 0;
}

extern void simple_pebs_entry(void);

#ifdef CONFIG_64BIT

asm("    .globl simple_pebs_entry\n"
    "simple_pebs_entry:\n"
    "    cld\n"
    "    testq $3,8(%rsp)\n"
    "    jz    1f\n"
    "    swapgs\n"
    "1:\n"
    "    pushq $0\n" /* error code */
    "    pushq %rdi\n"
    "    pushq %rsi\n"
    "    pushq %rdx\n"
    "    pushq %rcx\n"
    "    pushq %rax\n"
    "    pushq %r8\n"
    "    pushq %r9\n"
    "    pushq %r10\n"
    "    pushq %r11\n"
    "    pushq %rbx\n"
    "    pushq %rbp\n"
    "    pushq %r12\n"
    "    pushq %r13\n"
    "    pushq %r14\n"
    "    pushq %r15\n"
    "1:    call simple_pebs_pmi\n"
    "    popq %r15\n"
    "    popq %r14\n"
    "    popq %r13\n"
    "    popq %r12\n"
    "    popq %rbp\n"
    "    popq %rbx\n"
    "    popq %r11\n"
    "    popq %r10\n"
    "    popq %r9\n"
    "    popq %r8\n"
    "    popq %rax\n"
    "    popq %rcx\n"
    "    popq %rdx\n"
    "    popq %rsi\n"
    "    popq %rdi\n"
    "    addq $8,%rsp\n" /* error code */
    "    testq $3,8(%rsp)\n"
    "    jz 2f\n"
    "    swapgs\n"
    "2:\n"
    "    iretq");

#else
#error write me
#endif

static int pebs_group_lbr_depth(u64 format_size)
{
    return ((format_size >> KPERF_PEBS_DATACFG_LBR_SHIFT) & 0xff) + 1;
}

void simple_pebs_pmi(void)
{
    struct s_debug_store *ds;
    u8 *p, *end;
    u8 *outp, *outend;

    wrmsrl(MSR_IA32_PERF_GLOBAL_CTRL, 0);

    ds = __this_cpu_read(cpu_ds);
    p = (u8 *)ds->pebs_base;
    end = (u8 *)ds->pebs_index;

    outp = (u8 *)__this_cpu_read(out_buffer);
    outend = (u8 *)__this_cpu_read(out_buffer_end);

    while (p + sizeof(struct pebs_basic) <= end) {
        u64 group;
        u16 sz;
        int depth, real_depth, i;
        size_t rec_size, max_lbr_by_sz;
        struct pebs_basic *b;
        struct simple_pebs_out_rec *r;
        struct pebs_lbr_entry *lbr;

        b = (struct pebs_basic *)p;
        group = PEBS_GROUP(b);
        sz = PEBS_SIZE_BYTES(b);

        if (sz < sizeof(*b) || p + sz > end) {
            break;
        }

        /* depth from PEBS group (if LBRS present) */
        depth = 0;
        if (group & KPERF_PEBS_DATACFG_LBRS) {
            depth = pebs_group_lbr_depth(group);
        }
        depth = min(depth, SIMPLE_PEBS_MAX_LBR);

        rec_size = sizeof(struct simple_pebs_out_rec);
        if (outp + rec_size > outend)
            break;

        r = (struct simple_pebs_out_rec *)outp;
        memset(r, 0, sizeof(*r));

        r->size = (u16)rec_size;
        r->cpu  = (u8)smp_processor_id();
        r->ip   = b->ip;
        r->tsc  = b->tsc;
        r->tid = (__u32)task_pid_nr(current);
        r->tgid = (__u32)task_tgid_nr(current);

        if ((long)b->ip < 0) {
            p += sz;
            continue;
        }

        if (depth) {
            max_lbr_by_sz = 0;
            if (sz > sizeof(struct pebs_basic)) {
                max_lbr_by_sz =
                    (sz - sizeof(struct pebs_basic)) / sizeof(struct pebs_lbr_entry);
            } else {
                max_lbr_by_sz = 0;
            }
            depth = min(depth, (int)max_lbr_by_sz);
        }

        real_depth = 0;
        if (depth) {
            lbr = (struct pebs_lbr_entry *)(b + 1);
            for (i = 0; i < depth; i++) {
                if (lbr[i].from == 0 && lbr[i].to == 0) {
                    break;
                }
                r->lbr_from[i] = lbr[i].from;
                r->lbr_to[i]   = lbr[i].to;
                r->lbr_info[i] = lbr[i].info;
                real_depth++;
            }
        }
        r->lbr_depth = (u8)real_depth;

        outp += rec_size;
        p += sz;
    }

    __this_cpu_write(out_buffer, outp);
    ds->pebs_index = ds->pebs_base;

    /* handle overflow control */
    wrmsrl(MSR_IA32_PERF_GLOBAL_OVF_CTRL,
            KPERF_GLOBAL_OVF_BUF | KPERF_GLOBAL_LBR_FRZ | KPERF_GLOBAL_OVF_PMC0);
    apic_eoi();
    apic_write(APIC_LVTPC, pebs_vector);
    wrmsrl(MSR_IA32_PERF_GLOBAL_CTRL, 1);
}

unsigned long *vectors;

/* Get vector */
static int simple_pebs_get_vector(void)
{
    gate_desc desc, *idt;
    /* find interrupt vector occupation bitmap */
    vectors = (unsigned long *)kallsyms_lookup_name("used_vectors");
    if (!vectors)
        vectors = (unsigned long *)kallsyms_lookup_name("system_vectors");
    if (!vectors) {
        pr_err("Could not resolve system/used vectors. Missing CONFIG_KALLSYMS_ALL?\n");
        return -1;
    }

    /* No locking */
    while (test_bit(pebs_vector, vectors)) {
        if (pebs_vector == 0x40) {
            pr_err("No free vector found\n");
            return -1;
        }
        pebs_vector--;
    }
    set_bit(pebs_vector, vectors);
    idt = (gate_desc *)kallsyms_lookup_name("idt_table");
    if (!idt) {
        pr_err("Could not resolve idt_table. Did you enable CONFIG_KALLSYMS_ALL?\n");
        return -1;
    }

    pack_gate(&desc, GATE_INTERRUPT, (unsigned long)simple_pebs_entry,
            0, 0, 0);
    write_idt_entry(idt, pebs_vector,&desc);
    return 0;
}

static void simple_pebs_free_vector(void)
{
    if (vectors) {
        clear_bit(pebs_vector, vectors);
    }
    /* Not restoring the IDT. Assume the kernel always inits when it reallocates */
}

static inline u8 clamp_lbr_depth(int v)
{
    if (v < 1) {
        v = 1;
    }
    if (v > 32) {
        v = 32;
    }
    return (u8)v;
}

static void setup_pebs_data_cfg(void)
{
    u64 cfg = 0;
    u8 depth = clamp_lbr_depth(lbr_depth);

    cfg |= KPERF_PEBS_DATACFG_LBRS;
    cfg |= ((u64)(depth - 1) & 0xff) << KPERF_PEBS_DATACFG_LBR_SHIFT;

    wrmsrl(KPERF_MSR_PEBS_DATA_CFG, cfg);

    rdmsrl(KPERF_MSR_PEBS_DATA_CFG, cfg);
    pr_info("cpu%d PEBS_DATA_CFG=%llx (lbr_depth=%u)\n",
            smp_processor_id(), cfg, depth);
}

static void setup_arch_lbr(u8 depth)
{
    u64 ctl = KPERF_ARCH_LBR_CTL_LBREN | KPERF_LBR_CTL_USER | KPERF_LBR_CTL_ANY; /*KPERF_LBR_CTL_KERNEL*/

    /* safe on CPUs that don't support it */
    if (!wrmsrl_safe(KPERF_MSR_ARCH_LBR_DEPTH, depth))
        wrmsrl_safe(KPERF_MSR_ARCH_LBR_CTL, ctl);
}

static void simple_pebs_cpu_init(void *arg)
{
    u64 val, evtsel;
    unsigned long old_ds;

    /* Set up DS and buffer */
    if (__this_cpu_read(cpu_ds) == NULL) {
        if (allocate_buffer() < 0) {
            pebs_error = 1;
            return;
        }
    }

    if (__this_cpu_read(out_buffer) == NULL) {
        if (allocate_out_buf() < 0) { 
            pebs_error = 1;
            return;
        }
    }

    init_waitqueue_head(this_cpu_ptr(&simple_pebs_wait));

    /* Check if someone else is using the PMU */
    rdmsrl(MSR_IA32_EVNTSEL0, val);
    if (val & EVTSEL_EN) {
        pr_err("Someone else using perf counter 0\n");
        pebs_error = 1;
        return;
    }

    /* Set up DS */
    rdmsrl(MSR_IA32_DS_AREA, old_ds);
    __this_cpu_write(cpu_old_ds, old_ds);
    wrmsrl(MSR_IA32_DS_AREA, (unsigned long)__this_cpu_read(cpu_ds));

    /* Set up LVT */
    __this_cpu_write(old_lvtpc, apic_read(APIC_LVTPC));
    apic_write(APIC_LVTPC, pebs_vector);

    /* Initialize lbr */
    setup_pebs_data_cfg();
    setup_arch_lbr(clamp_lbr_depth(lbr_depth));

    /* First disable PMU to avoid races */
    wrmsrl(MSR_IA32_PERF_GLOBAL_CTRL, 0);

    wrmsrl(MSR_IA32_PERFCTR0, -PERIOD); /* ? sign extension on width ? */

    evtsel = pebs_event | EVTSEL_EN | EVTSEL_USR | EVTSEL_INT; /* EVTSEL_OS */
    if (pebs_baseline) {
        evtsel |= KPERF_ICL_EVENTSEL_ADAPTIVE;
    }
    wrmsrl(MSR_IA32_EVNTSEL0, evtsel);

    /* Enable PEBS for counter 0 */
    wrmsrl(MSR_IA32_PEBS_ENABLE, 1);

    wrmsrl(MSR_IA32_PERF_GLOBAL_CTRL, 1);
    __this_cpu_write(cpu_initialized, 1);
}

static void simple_pebs_cpu_reset(void *arg)
{
    if (__this_cpu_read(cpu_initialized)) {
        wrmsrl(MSR_IA32_PERF_GLOBAL_CTRL, 0);
        wrmsrl(MSR_IA32_PEBS_ENABLE, 0);
        wrmsrl(MSR_IA32_EVNTSEL0, 0);
        wrmsrl(MSR_IA32_PERFCTR0, 0);
        wrmsrl(MSR_IA32_DS_AREA, __this_cpu_read(cpu_old_ds));
        apic_write(APIC_LVTPC, __this_cpu_read(old_lvtpc));
        __this_cpu_write(cpu_initialized, 0);
    }
    if (__this_cpu_read(out_buffer_base)) {
        free_pages((unsigned long)__this_cpu_read(out_buffer_base),
                get_order(OUT_BUFFER_SIZE));
        __this_cpu_write(out_buffer_base, NULL);
        __this_cpu_write(out_buffer, NULL);
        __this_cpu_write(out_buffer_end, NULL);
    }
    if (__this_cpu_read(cpu_ds)) {
        struct s_debug_store *ds = __this_cpu_read(cpu_ds);
        kfree((void *)ds->pebs_base);
        kfree(ds);
        __this_cpu_write(cpu_ds, 0);
    }
}

static int simple_pebs_cpu(struct notifier_block *nb, unsigned long action, void *v)
{
    switch (action) {
    case CPU_STARTING:
        simple_pebs_cpu_init(NULL);
        break;
    case CPU_DYING:
        simple_pebs_cpu_reset(NULL);
        break;
    }
    return NOTIFY_OK;
}

static struct notifier_block cpu_notifier = {
    .notifier_call = simple_pebs_cpu,
};

static int simple_pebs_init(void)
{
    int err;

    if (!check_cpu())
        return -EIO;

    err = simple_pebs_get_vector();
    if (err < 0)
        return -EIO;

    get_online_cpus();
    on_each_cpu(simple_pebs_cpu_init, NULL, 1);
    register_cpu_notifier(&cpu_notifier);
    put_online_cpus();
    if (pebs_error) {
        pr_err("PEBS initialization failed\n");
        err = -EIO;
        goto out_notifier;
    }

    err = misc_register(&simple_pebs_miscdev);
    if (err < 0) {
        pr_err("Cannot register simple-pebs device\n");
        goto out_notifier;
    }
    pr_info("Initialized\n");

    return 0;

out_notifier:
    unregister_cpu_notifier(&cpu_notifier);
    on_each_cpu(simple_pebs_cpu_reset, NULL, 1);
    simple_pebs_free_vector();
    return err; 
}
module_init(simple_pebs_init);

static void simple_pebs_exit(void)
{
    misc_deregister(&simple_pebs_miscdev);
    get_online_cpus();
    on_each_cpu(simple_pebs_cpu_reset, NULL, 1);
    unregister_cpu_notifier(&cpu_notifier);
    put_online_cpus();
    simple_pebs_free_vector();
    /* Could PMI still be pending? For now just wait a bit. (XXX) */
    schedule_timeout(HZ);
    pr_info("Exited\n");
}

module_exit(simple_pebs_exit)
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Andi Kleen");
MODULE_DESCRIPTION("Minimal Linux PEBS driver with enhancements (Mulan PSL v2 build)");