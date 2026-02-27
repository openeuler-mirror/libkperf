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
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/poll.h>
#include <linux/percpu.h>
#include <linux/wait.h>
#include <linux/irq_work.h>
#include <linux/smp.h>
#include <linux/pid_namespace.h>
#include <linux/pid.h>
#include <linux/nmi.h>
#include <linux/rcupdate.h>
#include <asm/msr.h>
#include <asm/apic.h>
#include <asm/apicdef.h>
#include <cpuid.h>

#include "simple-pebs.h"

#define MSR_IA32_PERFCTR0      0x000000c1
#define MSR_IA32_EVNTSEL0      0x00000186

#define MSR_IA32_PERF_CAPABILITIES   0x00000345
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

static unsigned pebs_event = 0x003c;

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

/* per-CPU runtime state for simple-pebs*/
struct simple_pebs_cpu_state {
    struct s_debug_store *ds;
    void *pebs_buf;  /* PEBS record buffer */

    /* Output buffer */
    void *out_base;
    void *out_ptr;
    void *out_end;

    wait_queue_head_t waitq; /* poll/select wait queue for user space readers */
    struct irq_work wake_work; /* wakeup helper for NMI context */

    bool active; /* whether sampling is currently active on this CPU */
    struct file *owner; /* the fd that started sampling on this CPU */
    bool allocated; /* true if buffers were successfully allocated fot this CPU */

    /* restore when stop */
    u64 old_ds_area;
    u64 old_pebs_data_cfg;
    u64 old_debugctl;

    u64 old_arch_lbr_ctl;
    u64 old_arch_lbr_depth;

    u64 old_pebs_enable;
    u64 old_global_ctrl;
    u64 old_evntsel;

    u32 old_lvtpc;

    /* programmed value for NMI delivery (APIC_DM_NMI, unmasked) */
    u32 lvtpc_nmi;
};
static DEFINE_PER_CPU(struct simple_pebs_cpu_state, cpu_state);

struct cpu_req {
    struct file *file;
    int ret;
};

/* reopen LVT and clear mask to allow the next NMI delivery */
static inline void lvtpc_unmask(struct simple_pebs_cpu_state *st)
{
    apic_write(APIC_LVTPC, st->lvtpc_nmi & ~APIC_LVT_MASKED);
}

struct simple_pebs_file_ctx {
    int cpu;                    /* -1: not bound */
    struct pid_namespace *pidns;/* opener pidns (refcounted) */
};

/*
 * Per-CPU binding is RCU-protected because PMI (interrupt context)
 * may read it concurrently with ioctl/close updates.
 */
struct pebs_pidns_binding {
    struct pid_namespace *ns;   /* refcounted */
    struct file *owner;         /* which file bound this cpu */
    struct rcu_head rcu;
};

static DEFINE_PER_CPU(struct pebs_pidns_binding __rcu *, pebs_pidns_percpu);

static void pebs_pidns_binding_rcu_free(struct rcu_head *rh)
{
    struct pebs_pidns_binding *b = container_of(rh, struct pebs_pidns_binding, rcu);
    if (b->ns)
        put_pid_ns(b->ns);
    kfree(b);
}

static bool check_cpu(void)
{
    unsigned a, b, c, d;
    unsigned max, fam;
    unsigned feat1, feat2;
    
    __cpuid(0, max, b, c, d);
    if (memcmp(&b, "Genu", 4)) {
        pr_err("Not an Intel CPU\n");
        return false;
    }

    __cpuid(1, a, b, feat1, feat2);
    fam = (a >> 8) & 0xf;
    if (fam != 6) {
        pr_err("Not an supported Intel CPU\n");
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
    struct simple_pebs_file_ctx *ctx = file->private_data;
    int cpu;

    if (!ctx || ctx->cpu < 0)
        return -EINVAL;
    cpu = ctx->cpu;

    if (len % PAGE_SIZE || len != OUT_BUFFER_SIZE || vma->vm_pgoff)
        return -EINVAL;

    if (vma->vm_flags & VM_WRITE)
        return -EPERM;

    if (!cpu_online(cpu))
        return -EIO;

    return remap_pfn_range(vma, vma->vm_start,
                __pa(per_cpu(cpu_state, cpu).out_base) >> PAGE_SHIFT,
                OUT_BUFFER_SIZE,
                vma->vm_page_prot);
}

static unsigned int simple_pebs_poll(struct file *file, poll_table *wait)
{
    struct simple_pebs_file_ctx *ctx = file->private_data;
    unsigned long cpu;
    struct simple_pebs_cpu_state *st;

    if (!ctx || ctx->cpu < 0)
        return 0;
    cpu = (unsigned long)ctx->cpu;
    st = &per_cpu(cpu_state, cpu);

    poll_wait(file, &st->waitq, wait);
    if (READ_ONCE(st->out_ptr) > st->out_base)
        return POLLIN | POLLRDNORM;
    return 0;
}

static DEFINE_MUTEX(reset_mutex);

static void unbind_cpu_if_owner(struct file *file, int cpu)
{
    struct pebs_pidns_binding *old;

    if (cpu < 0 || cpu >= NR_CPUS)
        return;

    old = rcu_dereference_protected(per_cpu(pebs_pidns_percpu, cpu), 1);
    if (old && old->owner == file) {
        rcu_assign_pointer(per_cpu(pebs_pidns_percpu, cpu), NULL);
        call_rcu(&old->rcu, pebs_pidns_binding_rcu_free);
    }
}

static int bind_cpu_pidns(struct file *file, int cpu, struct pid_namespace *pidns)
{
    struct pebs_pidns_binding *old, *b;

    if (cpu < 0 || cpu >= NR_CPUS || !pidns)
        return -EINVAL;

    b = kzalloc(sizeof(*b), GFP_KERNEL);
    if (!b)
        return -ENOMEM;

    b->owner = file;
    b->ns = get_pid_ns(pidns);

    old = rcu_dereference_protected(per_cpu(pebs_pidns_percpu, cpu), 1);
    rcu_assign_pointer(per_cpu(pebs_pidns_percpu, cpu), b);
    if (old)
        call_rcu(&old->rcu, pebs_pidns_binding_rcu_free);

    return 0;
}


static void simple_pebs_cpu_init(void *info);
static void simple_pebs_cpu_stop(void *info);
static void simple_pebs_cpu_reset(void *info);

static int simple_pebs_open(struct inode *inode, struct file *file)
{
    struct simple_pebs_file_ctx *ctx;

    ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
    if (!ctx)
        return -ENOMEM;

    ctx->cpu = -1;
    ctx->pidns = get_pid_ns(task_active_pid_ns(current));
    file->private_data = ctx;
    return 0;
}

static int simple_pebs_release(struct inode *inode, struct file *file)
{
    struct simple_pebs_file_ctx *ctx = file->private_data;

    if (ctx) {
        /*
         * Best-effort: stop sampling on bound cpu before unbind
         * to reduce chance of PMI racing with teardown.
         */
        if (ctx->cpu >= 0 && ctx->cpu < NR_CPUS) {
            struct cpu_req req = { .file = file, .ret = 0 };
            mutex_lock(&reset_mutex);
            smp_call_function_single(ctx->cpu, simple_pebs_cpu_stop, &req, 1);
            mutex_unlock(&reset_mutex);
        }

        unbind_cpu_if_owner(file, ctx->cpu);

        if (ctx->pidns)
            put_pid_ns(ctx->pidns);

        kfree(ctx);
        file->private_data = NULL;
    }
    return 0;
}

static long simple_pebs_ioctl(struct file *file, unsigned int cmd,
                        unsigned long arg)
{
    struct simple_pebs_file_ctx *ctx = file->private_data;
    unsigned long cpu;

    if (!ctx)
        return -EINVAL;
    cpu = (ctx->cpu < 0) ? 0 : (unsigned long)ctx->cpu;

    switch (cmd) {
    case SIMPLE_PEBS_SET_CPU: {
        int new_cpu = (int)arg;
        int ret;

        if (new_cpu < 0 || new_cpu >= NR_CPUS || !cpu_online(new_cpu))
             return -EINVAL;
        /* Unbind old cpu if this fd owned it */
        unbind_cpu_if_owner(file, ctx->cpu);
        ctx->cpu = new_cpu;
        /* Bind opener pidns to this cpu */
        ret = bind_cpu_pidns(file, new_cpu, ctx->pidns);
        if (ret)
            return ret;

         return 0;
    }
    case SIMPLE_PEBS_GET_SIZE:
        return put_user(OUT_BUFFER_SIZE, (int __user *)arg);
    case SIMPLE_PEBS_GET_OFFSET: {
        if (ctx->cpu < 0)
            return -EINVAL;
        cpu = (unsigned long)ctx->cpu;
        {
            struct simple_pebs_cpu_state *st = &per_cpu(cpu_state, cpu);
            unsigned len = (unsigned)((char *)READ_ONCE(st->out_ptr) - (char *)st->out_base);
            return put_user(len, (unsigned __user *)arg);
        }
    }
    case SIMPLE_PEBS_START:
        /* reset out_buffer and pebs_index to get new data */
        if (ctx->cpu < 0)
            return -EINVAL;
        cpu = (unsigned long)ctx->cpu;
        {
            /* check permission/owner and get result */
            struct cpu_req req = { .file = file, .ret = 0 };
            mutex_lock(&reset_mutex);
            smp_call_function_single(cpu, simple_pebs_cpu_init, &req, 1);
            mutex_unlock(&reset_mutex);
            return req.ret;
        }
    case SIMPLE_PEBS_STOP:
        if (ctx->cpu < 0)
            return -EINVAL;
        cpu = (unsigned long)ctx->cpu;
        {
            struct cpu_req req = { .file = file, .ret = 0 };
            mutex_lock(&reset_mutex);
            smp_call_function_single(cpu, simple_pebs_cpu_stop, &req, 1);
            mutex_unlock(&reset_mutex);
            return req.ret;
        }
    case SIMPLE_PEBS_RESET:
        if (ctx->cpu < 0)
            return -EINVAL;
        cpu = (unsigned long)ctx->cpu;
        {
            struct cpu_req req = { .file = file, .ret = 0 };
            mutex_lock(&reset_mutex);
            smp_call_function_single(cpu, simple_pebs_cpu_reset, &req, 1);
            mutex_unlock(&reset_mutex);
            return req.ret;
        }
    default:
        return -ENOTTY;
    }
}

static const struct file_operations simple_pebs_fops = {
    .owner = THIS_MODULE,
    .open = simple_pebs_open,
    .release = simple_pebs_release,
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

/* notify poll/select waiters from irq_work context (not in NMI) */
static void simple_pebs_wake_workfn(struct irq_work *w)
{
    struct simple_pebs_cpu_state *st = container_of(w, struct simple_pebs_cpu_state, wake_work);

    wake_up_interruptible(&st->waitq);
}

/* Allocate DS and PEBS buffer */
static int allocate_buffer(void)
{
    int cpu;
    int ret = 0;

    for_each_possible_cpu(cpu) {
        struct simple_pebs_cpu_state *st = &per_cpu(cpu_state, cpu);
        unsigned long out_addr;

        memset(st, 0, sizeof(*st));
        st->ds = (struct s_debug_store *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, 0);
        if (!st->ds) {
            ret = -ENOMEM;
            goto fail;
        }
        st->pebs_buf = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, get_order(S_PEBS_BUFFER_SIZE));
        if (!st->pebs_buf) {
            ret = -ENOMEM;
            goto fail;
        }
        out_addr = __get_free_pages(GFP_KERNEL | __GFP_ZERO, get_order(OUT_BUFFER_SIZE));
        if (!out_addr) {
            ret = -ENOMEM;
            goto fail;
        }

        st->out_base = (void *)out_addr;
        st->out_ptr  = st->out_base;
        st->out_end  = (void *)(out_addr + OUT_BUFFER_SIZE);

        init_waitqueue_head(&st->waitq);  /* poll/select wait queue*/
        init_irq_work(&st->wake_work, simple_pebs_wake_workfn);

        st->active = false;
        st->owner  = NULL;
        st->allocated = true;
    }
    return 0;

fail:
    for_each_possible_cpu(cpu) {
        struct simple_pebs_cpu_state *st;
        st = &per_cpu(cpu_state, cpu);
        if (!st->allocated)
            continue;

        if (st->out_base) {
            free_pages((unsigned long)st->out_base, get_order(OUT_BUFFER_SIZE));
            st->out_base = NULL;
            st->out_ptr = NULL;
            st->out_end = NULL;
        }
        if (st->pebs_buf) {
            free_pages((unsigned long)st->pebs_buf, get_order(S_PEBS_BUFFER_SIZE));
            st->pebs_buf = NULL;
        }
        if (st->ds) {
            free_pages((unsigned long)st->ds, 0);
            st->ds = NULL;
        }
    }
    return ret;
}

static void free_buffer(void)
{
    int cpu;
    for_each_possible_cpu(cpu) {
        struct simple_pebs_cpu_state *st = &per_cpu(cpu_state, cpu);
        if (!st->allocated)
            continue;

        if (st->out_base) {
            free_pages((unsigned long)st->out_base, get_order(OUT_BUFFER_SIZE));
            st->out_base = NULL;
            st->out_ptr = NULL;
            st->out_end = NULL;
        }
        if (st->pebs_buf) {
            free_pages((unsigned long)st->pebs_buf, get_order(S_PEBS_BUFFER_SIZE));
            st->pebs_buf = NULL;
        }
        if (st->ds) {
            free_pages((unsigned long)st->ds, 0);
            st->ds = NULL;
        }
    }
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

static int pebs_group_lbr_depth(u64 format_size)
{
    return ((format_size >> KPERF_PEBS_DATACFG_LBR_SHIFT) & 0xff) + 1;
}

static int simple_pebs_nmi_handler(unsigned int val, struct pt_regs *regs)
{
    struct s_debug_store *ds;
    u8 *p, *end;
    u8 *outp, *outend;

    struct simple_pebs_cpu_state *st = this_cpu_ptr(&cpu_state);
    u64 status, ctrl_before, ctrl_disable;
    u8 depth_req;
    bool has_ovf, has_pebs;

    (void)val;
    (void)regs;

    if (!READ_ONCE(st->active))
        return NMI_DONE;

    ds = READ_ONCE(st->ds);
    if (!ds)
        return NMI_DONE;

    rdmsrl(MSR_IA32_PERF_GLOBAL_STATUS, status);

    has_ovf  = !!(status & (KPERF_GLOBAL_PMC0 | KPERF_GLOBAL_OVF_BUF | KPERF_GLOBAL_LBR_FRZ));
    has_pebs = (READ_ONCE(ds->pebs_index) != READ_ONCE(ds->pebs_base));

    if (!has_ovf && !has_pebs) {
        lvtpc_unmask(st);
        return NMI_HANDLED;
    }

    rdmsrl(MSR_IA32_PERF_GLOBAL_CTRL, ctrl_before);
    ctrl_disable = ctrl_before & ~KPERF_GLOBAL_PMC0;
    wrmsrl(MSR_IA32_PERF_GLOBAL_CTRL, ctrl_disable);

    p = (u8 *)(unsigned long)READ_ONCE(ds->pebs_base);
    end = (u8 *)(unsigned long)READ_ONCE(ds->pebs_index);
    outp = (u8 *)READ_ONCE(st->out_ptr);
    outend = (u8 *)READ_ONCE(st->out_end);

    depth_req = clamp_lbr_depth(lbr_depth);

    while (p + sizeof(struct pebs_basic) <= end) {
        u64 group;
        u16 sz;
        int depth = 0, real_depth = 0, i;
        struct pebs_basic *b;
        struct simple_pebs_out_rec *r;
        struct pebs_lbr_entry *lbr;
        size_t rec_size = sizeof(*r);
        size_t lbr_bytes;

        b = (struct pebs_basic *)p;
        group = PEBS_GROUP(b);
        sz = PEBS_SIZE_BYTES(b);

        if (sz < sizeof(*b) || p + sz > end) {
            break;
        }

        if (outp + rec_size > outend)
            break;

        r = (struct simple_pebs_out_rec *)outp;
        memset(r, 0, sizeof(*r));

        r->size = (u16)rec_size;
        r->cpu  = (u8)smp_processor_id();
        r->ip   = b->ip;
        r->tsc  = b->tsc;
        {
            struct pebs_pidns_binding *bind;
            struct pebs_pidns_binding __rcu *pns;
            struct pid_namespace *ns;
            rcu_read_lock();
            pns = this_cpu_read(pebs_pidns_percpu);
            bind = rcu_dereference(pns);

            ns = (bind && bind->ns) ? bind->ns : task_active_pid_ns(current);
            r->tid  = (__u32)task_pid_nr_ns(current, ns);
            r->tgid = (__u32)task_tgid_nr_ns(current, ns);
            rcu_read_unlock();
        }
        if ((long)b->ip < 0) {
            p += sz;
            continue;
        }

        if ((group & KPERF_PEBS_DATACFG_LBRS) != 0) {
            depth = pebs_group_lbr_depth(group);
            if (depth > SIMPLE_PEBS_MAX_LBR)
                depth = SIMPLE_PEBS_MAX_LBR;
            if (depth > depth_req)
                depth = depth_req;
        }

        if (depth > 0) {
            lbr_bytes = (size_t)depth * sizeof(struct pebs_lbr_entry);
            if (sz >= sizeof(*b) + lbr_bytes) {
                lbr = (struct pebs_lbr_entry *)(p + sz - lbr_bytes);
                for (i = 0; i < depth; i++) {
                    if (lbr[i].from == 0 && lbr[i].to == 0)
                        break;
                    r->lbr_from[i] = lbr[i].from;
                    r->lbr_to[i]   = lbr[i].to;
                    r->lbr_info[i] = lbr[i].info;
                    real_depth++;
                }
            }
        }
        r->lbr_depth = (u8)real_depth;

        outp += rec_size;
        p += sz;
    }

    WRITE_ONCE(st->out_ptr, outp);
    WRITE_ONCE(ds->pebs_index, ds->pebs_base);

    /* handle overflow control + reload counter */
    wrmsrl(MSR_IA32_PERF_GLOBAL_OVF_CTRL,
           KPERF_GLOBAL_OVF_BUF | KPERF_GLOBAL_LBR_FRZ | KPERF_GLOBAL_PMC0);
    wrmsrl(MSR_IA32_PERFCTR0, -(long long)PERIOD);

    /* re-enable */
    wrmsrl(MSR_IA32_PERF_GLOBAL_CTRL, ctrl_before);

    lvtpc_unmask(st);

    irq_work_queue(&st->wake_work);
    return NMI_HANDLED;
}

static void setup_pebs_data_cfg(struct simple_pebs_cpu_state *st)
{
    u64 cfg = 0;
    u8 depth = clamp_lbr_depth(lbr_depth);

    cfg |= KPERF_PEBS_DATACFG_LBRS;
    cfg |= ((u64)(depth - 1) & 0xff) << KPERF_PEBS_DATACFG_LBR_SHIFT;

    rdmsrl(KPERF_MSR_PEBS_DATA_CFG, st->old_pebs_data_cfg);
    wrmsrl(KPERF_MSR_PEBS_DATA_CFG, cfg);
}

static void setup_arch_lbr(struct simple_pebs_cpu_state *st, u8 depth)
{
    u64 ctl = KPERF_ARCH_LBR_CTL_LBREN | KPERF_LBR_CTL_USER | KPERF_LBR_CTL_ANY; /*KPERF_LBR_CTL_KERNEL*/
    u64 debugctl;

    /* safe on CPUs that don't support it */
    rdmsrl(KPERF_MSR_IA32_DEBUGCTLMSR, st->old_debugctl);
    debugctl = st->old_debugctl | DEBUGCTLMSR_FREEZE_LBRS_ON_PMI;
    rdmsrl_safe(KPERF_MSR_ARCH_LBR_CTL, &st->old_arch_lbr_ctl);
    rdmsrl_safe(KPERF_MSR_ARCH_LBR_DEPTH, &st->old_arch_lbr_depth);
    if (!wrmsrl_safe(KPERF_MSR_ARCH_LBR_DEPTH, depth))
        wrmsrl_safe(KPERF_MSR_ARCH_LBR_CTL, ctl);
    wrmsrl(KPERF_MSR_IA32_DEBUGCTLMSR, debugctl);
}

static void simple_pebs_cpu_init(void *info)
{
    u64 val, evtsel, ctrl, pebs_enable;
    struct cpu_req *req = info;
    struct simple_pebs_cpu_state *st = this_cpu_ptr(&cpu_state);
    struct s_debug_store *ds = st->ds;

    if (!ds || !st->pebs_buf || !st->out_base) {
        req->ret = -ENOMEM;
        return;
    }

    if (st->active) {
        req->ret = (st->owner == req->file) ? 0 : -EBUSY;
        return;
    }

    /* Check if someone else is using the PMU */
    rdmsrl(MSR_IA32_EVNTSEL0, val);
    if (val & EVTSEL_EN) {
        req->ret = -EBUSY;
        return;
    }
    rdmsrl(MSR_IA32_PEBS_ENABLE, pebs_enable);
    if (pebs_enable != 0) {
        req->ret = -EBUSY;
        return;
    }

    /* save old status */
    rdmsrl(MSR_IA32_DS_AREA, st->old_ds_area);
    rdmsrl(MSR_IA32_PEBS_ENABLE, st->old_pebs_enable);
    rdmsrl(MSR_IA32_PERF_GLOBAL_CTRL, st->old_global_ctrl);
    rdmsrl(MSR_IA32_EVNTSEL0, st->old_evntsel);

    /* Set up DS and buffer */
    ds->pebs_base  = (u64)(unsigned long)st->pebs_buf;
    ds->pebs_index = ds->pebs_base;
    ds->pebs_max   = ds->pebs_base + S_PEBS_BUFFER_SIZE;
    ds->pebs_thresh = ds->pebs_max - 4096; /* threshold is set to the end of buffer to get more records */
    if (ds->pebs_thresh < ds->pebs_base + 4096)
        ds->pebs_thresh = ds->pebs_base + 4096;
    if (ds->pebs_thresh >= ds->pebs_max)
        ds->pebs_thresh = ds->pebs_max - 1;
    memset(ds->pebs_reset, 0, sizeof(ds->pebs_reset));
    ds->pebs_reset[0] = -(long long)PERIOD;
    wrmsrl(MSR_IA32_DS_AREA, (u64)(unsigned long)ds);
    WRITE_ONCE(st->out_ptr, st->out_base);

    /* Set up LVT */
    st->old_lvtpc = apic_read(APIC_LVTPC);
    st->lvtpc_nmi = (st->old_lvtpc & ~APIC_MODE_MASK) | APIC_DM_NMI;
    st->lvtpc_nmi &= ~APIC_LVT_MASKED;
    apic_write(APIC_LVTPC, st->lvtpc_nmi);

    /* Initialize lbr */
    setup_arch_lbr(st, clamp_lbr_depth(lbr_depth));
    setup_pebs_data_cfg(st);

    /* First disable PMU to avoid races */
    rdmsrl(MSR_IA32_PERF_GLOBAL_CTRL, ctrl);
    wrmsrl(MSR_IA32_PERF_GLOBAL_CTRL, ctrl & ~KPERF_GLOBAL_PMC0);

    wrmsrl(MSR_IA32_PERFCTR0, -(long long)PERIOD);

    evtsel = pebs_event | EVTSEL_EN | EVTSEL_USR | EVTSEL_INT;  /* add EVTSEL_OS if need*/
    if (!wrmsrl_safe(MSR_IA32_EVNTSEL0, evtsel | KPERF_ICL_EVENTSEL_ADAPTIVE)) {
    } else {
        wrmsrl(MSR_IA32_EVNTSEL0, evtsel);
    }

    /* Enable PEBS for counter 0*/
    wrmsrl(MSR_IA32_PEBS_ENABLE, KPERF_GLOBAL_PMC0);
    wrmsrl(MSR_IA32_PERF_GLOBAL_OVF_CTRL,
           KPERF_GLOBAL_OVF_BUF | KPERF_GLOBAL_LBR_FRZ | KPERF_GLOBAL_PMC0);

    st->owner = req->file;
    WRITE_ONCE(st->active, true);

    rdmsrl(MSR_IA32_PERF_GLOBAL_CTRL, ctrl);
    wrmsrl(MSR_IA32_PERF_GLOBAL_CTRL, ctrl | KPERF_GLOBAL_PMC0);

    req->ret = 0;
}

static void simple_pebs_cpu_stop(void *info)
{
    struct cpu_req *req = info;
    struct simple_pebs_cpu_state *st = this_cpu_ptr(&cpu_state);
    u64 ctrl;
    u32 lvtpc;

    if (!st->active) {
        req->ret = 0;
        return;
    }
    if (req->file && st->owner != req->file) {
        req->ret = 0;
        return;
    }

    rdmsrl(MSR_IA32_PERF_GLOBAL_CTRL, ctrl);
    wrmsrl(MSR_IA32_PERF_GLOBAL_CTRL, ctrl & ~KPERF_GLOBAL_PMC0);

    /* mask LVTPC*/
    lvtpc = apic_read(APIC_LVTPC);
    apic_write(APIC_LVTPC, lvtpc | APIC_LVT_MASKED);

    /* restore PEBS */
    wrmsrl(MSR_IA32_PEBS_ENABLE, st->old_pebs_enable);
    wrmsrl(MSR_IA32_EVNTSEL0, st->old_evntsel);

    wrmsrl(MSR_IA32_DS_AREA, st->old_ds_area);
    wrmsrl(KPERF_MSR_PEBS_DATA_CFG, st->old_pebs_data_cfg);

    /* lbr restore */
    wrmsrl_safe(KPERF_MSR_ARCH_LBR_CTL, st->old_arch_lbr_ctl);
    wrmsrl_safe(KPERF_MSR_ARCH_LBR_DEPTH, st->old_arch_lbr_depth);
    wrmsrl(KPERF_MSR_IA32_DEBUGCTLMSR, st->old_debugctl);
    /* restore LVTPC */
    apic_write(APIC_LVTPC, st->old_lvtpc);

    wrmsrl(MSR_IA32_PERF_GLOBAL_CTRL, st->old_global_ctrl);

    st->owner = NULL;
    WRITE_ONCE(st->active, false);
    req->ret = 0;
}

static void simple_pebs_cpu_reset(void *info)
{
    struct cpu_req *req = info;
    struct simple_pebs_cpu_state *st = this_cpu_ptr(&cpu_state);
    struct s_debug_store *ds = st->ds;
    u64 ctrl;

    if (!ds) {
        req->ret = -EINVAL;
        return;
    }

    rdmsrl(MSR_IA32_PERF_GLOBAL_CTRL, ctrl);
    wrmsrl(MSR_IA32_PERF_GLOBAL_CTRL, ctrl & ~KPERF_GLOBAL_PMC0);

    ds->pebs_index = ds->pebs_base;
    WRITE_ONCE(st->out_ptr, st->out_base);

    wrmsrl(MSR_IA32_PERFCTR0, -(long long)PERIOD);
    wrmsrl(MSR_IA32_PERF_GLOBAL_OVF_CTRL,
           KPERF_GLOBAL_OVF_BUF | KPERF_GLOBAL_LBR_FRZ | KPERF_GLOBAL_PMC0);

    if (READ_ONCE(st->active))
        lvtpc_unmask(st);

    wrmsrl(MSR_IA32_PERF_GLOBAL_CTRL, ctrl);
    req->ret = 0;
}

static int simple_pebs_init(void)
{
    int err;

    if (!check_cpu())
        return -EIO;

    err = allocate_buffer();
    if (err)
        goto out_free;

    err = register_nmi_handler(NMI_LOCAL, simple_pebs_nmi_handler, 0, "simple-pebs");
    if (err) {
        pr_err("register_nmi_handler failed: %d\n", err);
        goto out_free;
    }

    err = misc_register(&simple_pebs_miscdev);
    if (err) {
        pr_err("misc_register failed: %d\n", err);
        unregister_nmi_handler(NMI_LOCAL, "simple-pebs");
        goto out_free;
    }
    pr_info("Initialized: device=/dev/simple-pebs\n");

    return 0;

out_free:
    free_buffer();
    return err;
}
module_init(simple_pebs_init);

static void simple_pebs_exit(void)
{
    int cpu;
    misc_deregister(&simple_pebs_miscdev);
    for_each_online_cpu(cpu) {
        struct cpu_req req = { .file = NULL, .ret = 0 };
        smp_call_function_single(cpu, simple_pebs_cpu_stop, &req, 1);
    }
    unregister_nmi_handler(NMI_LOCAL, "simple-pebs");
    /* Clear all bindings and wait for RCU callbacks before module unload */
    for_each_possible_cpu(cpu) {
        struct pebs_pidns_binding *old;
        old = rcu_dereference_protected(per_cpu(pebs_pidns_percpu, cpu), 1);
        if (old) {
            rcu_assign_pointer(per_cpu(pebs_pidns_percpu, cpu), NULL);
            call_rcu(&old->rcu, pebs_pidns_binding_rcu_free);
        }
    }
    synchronize_rcu();

    free_buffer();
    pr_info("Exited\n");
}

module_exit(simple_pebs_exit)
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Andi Kleen");
MODULE_DESCRIPTION("Minimal Linux PEBS driver with enhancements (Mulan PSL v2 build)");