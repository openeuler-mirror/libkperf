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
 * Description: Provides the capability of LBR collection by tracepoint
******************************************************************************/
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
#include <linux/smp.h>
#include <linux/pid_namespace.h>
#include <linux/pid.h>
#include <linux/mutex.h>
#include <linux/ktime.h>
#include <linux/hrtimer.h>
#include <linux/tracepoint.h>
#include <linux/rcupdate.h>
#include <asm/msr.h>
#include <asm/msr-index.h>
#include <asm/ptrace.h>
#include <asm/irq_regs.h>
#include "simple-lbr-trace.h"

static struct tracepoint *sched_switch_tp;

/* sched_switch tracepoint is not exported in the kernel. Register after dynamic lookup */
static void find_sched_switch_tp(struct tracepoint *tp, void *priv)
{
    struct tracepoint **found = priv;

    if (*found)
        return;

    if (strcmp(tp->name, "sched_switch") == 0)
        *found = tp;
}

static int lbr_depth = SIMPLE_LBR_MAX_LBR;
module_param(lbr_depth, int, 0644);
MODULE_PARM_DESC(lbr_depth, "Arch LBR depth to use (1..32)");

static unsigned int sample_period = 500;
module_param(sample_period, uint, 0644);
MODULE_PARM_DESC(sample_period, "Periodic LBR sampling period in microseconds");

struct simple_lbr_file_ctx {
    struct pid_namespace *pidns;  /* opener pidns (refcounted) */
};

/* per-CPU runtime state for simple-lbr-trace */
struct simple_lbr_cpu_runtime {
    bool lbr_on;

    u64 old_debugctl;
    u64 old_arch_lbr_ctl;
    u64 old_arch_lbr_depth;

    struct simple_lbr_out_rec *records;
    u64 head_seq;       /* next sequence to write */
    u64 valid_tail_seq; /* oldest valid record still retained in ring */
    u64 consumed_seq;   /* userspace-reported consumed position */

    u64 overwrite_records;
    u64 enable_failures;

    struct hrtimer sample_timer;
    bool timer_started;
};
static DEFINE_PER_CPU(struct simple_lbr_cpu_runtime, cpu_state);

struct simple_lbr_dev_state {
    bool running;
    struct file *owner;
    int target_pid;
    struct pid_namespace *pidns;

    wait_queue_head_t waitq;
    struct mutex ctrl_lock;
};

static struct simple_lbr_dev_state lbr_dev;

static inline unsigned long lbr_out_records_bytes(void)
{
    return sizeof(struct simple_lbr_out_rec) * SIMPLE_LBR_RING_SLOTS;
}

static inline unsigned long simple_lbr_map_size(void)
{
    return PAGE_ALIGN(lbr_out_records_bytes());
}

static inline int check_owner(struct file *file)
{
    struct file *owner = READ_ONCE(lbr_dev.owner);
    if (!owner || owner != file)
        return -EBUSY;
    return 0;
}

static int acquire_owner_locked(struct file *file)
{
    struct file *owner = READ_ONCE(lbr_dev.owner);

    if (owner && owner != file)
        return -EBUSY;

    WRITE_ONCE(lbr_dev.owner, file);
    return 0;
}

static void clear_target_pid_locked(void)
{
    if (lbr_dev.pidns) {
        put_pid_ns(lbr_dev.pidns);
        lbr_dev.pidns = NULL;
    }
    lbr_dev.target_pid = 0;
}

static bool check_task_matches_target(struct task_struct *p)
{
    struct pid_namespace *ns;
    int tgid;

    if (!p)
        return false;

    ns = READ_ONCE(lbr_dev.pidns);
    tgid = READ_ONCE(lbr_dev.target_pid);
    if (!ns || tgid <= 0)
        return false;
    return task_tgid_nr_ns(p, ns) == tgid;
}

static inline u8 clamp_lbr_depth(int v)
{
    if (v < 1)
        v = 1;
    if (v > SIMPLE_LBR_MAX_LBR)
        v = SIMPLE_LBR_MAX_LBR;
    return (u8)v;
}

static inline bool is_valid_user_lbr_addr(u64 a)
{
    if (a == 0 || a == ~0ULL)
        return false;
    return ((a >> 63) == 0);
}

static u64 get_user_ip(void) {
    struct pt_regs *regs;
    regs = get_irq_regs();
    if(regs && user_mode(regs))
        return instruction_pointer(regs);
    return 0;
}

static int lbr_enable_cpu(struct simple_lbr_cpu_runtime *st)
{
    u64 ctl = KPERF_ARCH_LBR_CTL_LBREN | KPERF_LBR_CTL_USER | KPERF_LBR_CTL_ANY;
    u64 debugctl;
    u8 depth = clamp_lbr_depth(lbr_depth);

    if (st->lbr_on)
        return 0;
    if (rdmsrl_safe(KPERF_MSR_ARCH_LBR_CTL, &st->old_arch_lbr_ctl))
        goto fail_unsupported;
    if (rdmsrl_safe(KPERF_MSR_ARCH_LBR_DEPTH, &st->old_arch_lbr_depth))
        goto fail_unsupported;

    rdmsrl(KPERF_MSR_IA32_DEBUGCTLMSR, st->old_debugctl);
    debugctl = st->old_debugctl | DEBUGCTLMSR_FREEZE_LBRS_ON_PMI;
    if (wrmsrl_safe(KPERF_MSR_ARCH_LBR_DEPTH, (u64)depth))
        goto fail_io;
    if (wrmsrl_safe(KPERF_MSR_ARCH_LBR_CTL, ctl)) {
        wrmsrl_safe(KPERF_MSR_ARCH_LBR_DEPTH, st->old_arch_lbr_depth);
        goto fail_io;
    }
    wrmsrl(KPERF_MSR_IA32_DEBUGCTLMSR, debugctl);
    st->lbr_on = true;
    return 0;

fail_unsupported:
    st->enable_failures++;
    return -EOPNOTSUPP;

fail_io:
    st->enable_failures++;
    return -EIO;
}

static void lbr_disable_cpu(struct simple_lbr_cpu_runtime *st)
{
    if (!st->lbr_on)
        return;

    wrmsrl_safe(KPERF_MSR_ARCH_LBR_CTL, st->old_arch_lbr_ctl);
    wrmsrl_safe(KPERF_MSR_ARCH_LBR_DEPTH, st->old_arch_lbr_depth);
    wrmsrl(KPERF_MSR_IA32_DEBUGCTLMSR, st->old_debugctl);
    st->lbr_on = false;
}

static void lbr_clear_cpu(struct simple_lbr_cpu_runtime *st)
{
    int i;
    u8 depth;
    if (!st->lbr_on)
        return;

    depth = clamp_lbr_depth(lbr_depth);
    for (i = 0; i < depth; i++) {
        wrmsrl_safe(KPERF_MSR_ARCH_LBR_FROM_0 + i, 0);
        wrmsrl_safe(KPERF_MSR_ARCH_LBR_TO_0   + i, 0);
        wrmsrl_safe(KPERF_MSR_ARCH_LBR_INFO_0 + i, 0);
    }
}

static void append_record(const struct simple_lbr_out_rec *rec)
{
    struct simple_lbr_cpu_runtime *st = this_cpu_ptr(&cpu_state);
    u64 head, valid_tail;
    struct simple_lbr_out_rec *slot;

    if (!st->records)
        return;
    head = st->head_seq;
    valid_tail = READ_ONCE(st->valid_tail_seq);

    /* if ring is full, advance oldest valid sequence first, then write the newest record */
    if (head - valid_tail >= SIMPLE_LBR_RING_SLOTS) {
        valid_tail++;
        WRITE_ONCE(st->valid_tail_seq, valid_tail);
        st->overwrite_records++;
        if (READ_ONCE(st->consumed_seq) < valid_tail)
            WRITE_ONCE(st->consumed_seq, valid_tail);
    }
    slot = &st->records[head & (SIMPLE_LBR_RING_SLOTS - 1)];
    *slot = *rec;
    /* Publish new head only after record content is fully written */
    smp_store_release(&st->head_seq, head + 1);
    if (waitqueue_active(&lbr_dev.waitq))
        wake_up_interruptible(&lbr_dev.waitq);
}

static void lbr_to_buffer(struct task_struct *task, u16 reason)
{
    struct simple_lbr_out_rec rec;
    struct pid_namespace *ns;
    int i, real_depth = 0;
    u8 depth = clamp_lbr_depth(lbr_depth);

    memset(&rec, 0, sizeof(rec));
    rec.size = sizeof(rec);
    rec.reason = reason;
    rec.cpu = smp_processor_id();
    rec.tsc = ktime_get_ns();
    ns = READ_ONCE(lbr_dev.pidns);
    if (ns) {
        rec.tid = (__u32)task_pid_nr_ns(task, ns);
        rec.tgid = (__u32)task_tgid_nr_ns(task, ns);
    }
    rec.ip = get_user_ip();

    for (i = 0; i < depth; i++) {
        u64 from = 0, to = 0, info = 0;
        if (rdmsrl_safe(KPERF_MSR_ARCH_LBR_FROM_0 + i, &from))
            break;
        if (rdmsrl_safe(KPERF_MSR_ARCH_LBR_TO_0 + i, &to))
            break;
        if (rdmsrl_safe(KPERF_MSR_ARCH_LBR_INFO_0 + i, &info))
            break;
        if (from == 0 && to == 0)
            break;
        rec.lbr_from[i] = from;
        rec.lbr_to[i]   = to;
        rec.lbr_info[i] = info;
        /*
        another way to set sample ip:
        if (!rec.ip && is_valid_user_lbr_addr(to))
            rec.ip = to;
        */
        real_depth++;
    }

    rec.lbr_depth = real_depth;
    if (rec.lbr_depth <= 0 || !rec.ip)
        return;
    append_record(&rec);
}

static enum hrtimer_restart simple_lbr_sample_timer_fn(struct hrtimer *timer)
{
    struct simple_lbr_cpu_runtime *st = container_of(timer, struct simple_lbr_cpu_runtime, sample_timer);

    if (!READ_ONCE(lbr_dev.running))
        return HRTIMER_NORESTART;

    if (st->lbr_on && check_task_matches_target(current))
        lbr_to_buffer(current, SIMPLE_LBR_REASON_TIMER);

    hrtimer_forward_now(timer, ktime_set(0, (u64)sample_period * 1000));
    return HRTIMER_RESTART;
}

struct lbr_check_req {
    int ret;
};

static void lbr_check_cpu_ipi(void *info)
{
    struct lbr_check_req *req = info;
    u64 tmp;

    if (READ_ONCE(req->ret))
        return;

    if (rdmsrl_safe(KPERF_MSR_ARCH_LBR_CTL, &tmp) ||
        rdmsrl_safe(KPERF_MSR_ARCH_LBR_DEPTH, &tmp) ||
        rdmsrl_safe(KPERF_MSR_ARCH_LBR_FROM_0, &tmp) ||
        rdmsrl_safe(KPERF_MSR_ARCH_LBR_TO_0, &tmp) ||
        rdmsrl_safe(KPERF_MSR_ARCH_LBR_INFO_0, &tmp)) {
        WRITE_ONCE(req->ret, -EOPNOTSUPP);
    }
}

static int check_arch_lbr(void)
{
    struct lbr_check_req req = { .ret = 0 };

    get_online_cpus();
    on_each_cpu(lbr_check_cpu_ipi, &req, 1);
    put_online_cpus();

    return READ_ONCE(req.ret);
}

/* enable and reset LBR when the target is already running before START */
static void start_lbr_on_cpu_ipi(void *info)
{
    struct simple_lbr_cpu_runtime *st = this_cpu_ptr(&cpu_state);
    if (!READ_ONCE(lbr_dev.running))
        return;
    if (!check_task_matches_target(current))
        return;
    if (!st->lbr_on) {
        if (lbr_enable_cpu(st))
            return;
    }
    lbr_clear_cpu(st);
}

static void stop_lbr_on_cpu_ipi(void *info)
{
    struct simple_lbr_cpu_runtime *st = this_cpu_ptr(&cpu_state);

    if (st->lbr_on && check_task_matches_target(current))
        lbr_to_buffer(current, SIMPLE_LBR_REASON_STOP);

    if (st->lbr_on)
        lbr_disable_cpu(st);
}

static void start_timer_on_cpu_ipi(void *info)
{
    struct simple_lbr_cpu_runtime *st = this_cpu_ptr(&cpu_state);
    if (st->timer_started)
        return;

    hrtimer_start(&st->sample_timer, ktime_set(0, (u64)sample_period * 1000), HRTIMER_MODE_REL_PINNED);
    st->timer_started = true;
}

static void stop_timer_on_cpu_ipi(void *info)
{
    struct simple_lbr_cpu_runtime *st = this_cpu_ptr(&cpu_state);
    if (!st->timer_started)
        return;

    hrtimer_cancel(&st->sample_timer);
    st->timer_started = false;
}

static void simple_lbr_sched_switch_probe(void *ignore, bool preempt,
                                          struct task_struct *prev,
                                          struct task_struct *next)
{
    struct simple_lbr_cpu_runtime *st = this_cpu_ptr(&cpu_state);
    bool prev_match, next_match;
    (void)ignore;
    (void)preempt;

    if (!READ_ONCE(lbr_dev.running))
        return;

    prev_match = check_task_matches_target(prev);
    next_match = check_task_matches_target(next);
    if (prev_match && st->lbr_on) {
        lbr_to_buffer(prev, SIMPLE_LBR_REASON_SCHED_OUT);
        lbr_disable_cpu(st);
    }

    if (next_match) {
        if (!st->lbr_on) {
            if (lbr_enable_cpu(st))
                return;
        }
    }
}

static void free_buffer(void)
{
    int cpu;
    unsigned long map_size = simple_lbr_map_size();
    unsigned int order = get_order(map_size);

    for_each_possible_cpu(cpu) {
        struct simple_lbr_cpu_runtime *st = &per_cpu(cpu_state, cpu);
        if (st->timer_started) {
            hrtimer_cancel(&st->sample_timer);
            st->timer_started = false;
        }
        if (st->records) {
            free_pages((unsigned long)st->records, order);
            st->records = NULL;
        }
    }
}

static int allocate_buffer(void)
{
    int cpu;
    unsigned long map_size = simple_lbr_map_size();
    unsigned int order = get_order(map_size);

    for_each_possible_cpu(cpu) {
        struct simple_lbr_cpu_runtime *st = &per_cpu(cpu_state, cpu);
        unsigned long addr;
        memset(st, 0, sizeof(*st));
        addr = __get_free_pages(GFP_KERNEL | __GFP_ZERO, order);
        if (!addr) {
            free_buffer();
            return -ENOMEM;
        }

        st->records = (struct simple_lbr_out_rec *)addr;
        st->head_seq = 0;
        st->valid_tail_seq = 0;
        st->consumed_seq = 0;
        st->overwrite_records = 0;
        st->enable_failures = 0;
        st->timer_started = false;

        hrtimer_init(&st->sample_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL_PINNED);
        st->sample_timer.function = simple_lbr_sample_timer_fn;
    }
    return 0;
}

static void reset_buffer_locked(void)
{
    int cpu;
    unsigned long bytes = lbr_out_records_bytes();

    for_each_possible_cpu(cpu) {
        struct simple_lbr_cpu_runtime *st = &per_cpu(cpu_state, cpu);
        if (!st->records)
            continue;
        memset(st->records, 0, bytes);
        st->head_seq = 0;
        st->valid_tail_seq = 0;
        st->consumed_seq = 0;
        st->overwrite_records = 0;
        st->enable_failures = 0;
    }
}

static int simple_lbr_start(struct file *file)
{
    int ret = 0;
    mutex_lock(&lbr_dev.ctrl_lock);
    ret = acquire_owner_locked(file);
    if (ret)
        goto out;

    if (!lbr_dev.pidns || !lbr_dev.target_pid) {
        ret = -EINVAL;
        goto out;
    }

    if (READ_ONCE(lbr_dev.running))
        goto out;

    ret = check_arch_lbr();
    if (ret)
        goto out;

    reset_buffer_locked();

    sched_switch_tp = NULL;
    for_each_kernel_tracepoint(find_sched_switch_tp, &sched_switch_tp);
    if (!sched_switch_tp) {
        ret = -ENOENT;
        goto out;
    }
    ret = tracepoint_probe_register(sched_switch_tp,
                                    (void *)simple_lbr_sched_switch_probe,
                                    NULL);
    if (ret)
        goto out;

    WRITE_ONCE(lbr_dev.running, true);

    get_online_cpus();
    on_each_cpu(start_lbr_on_cpu_ipi, NULL, 1);
    on_each_cpu(start_timer_on_cpu_ipi, NULL, 1);
    put_online_cpus();
out:
    mutex_unlock(&lbr_dev.ctrl_lock);
    return ret;
}

static void simple_lbr_stop_locked(void)
{
    if (!READ_ONCE(lbr_dev.running))
        return;

    if (sched_switch_tp) {
        tracepoint_probe_unregister(sched_switch_tp,
                                    (void *)simple_lbr_sched_switch_probe,
                                    NULL);
        tracepoint_synchronize_unregister();
        sched_switch_tp = NULL;
    }

    WRITE_ONCE(lbr_dev.running, false);

    get_online_cpus();
    on_each_cpu(stop_timer_on_cpu_ipi, NULL, 1);
    on_each_cpu(stop_lbr_on_cpu_ipi, NULL, 1);
    put_online_cpus();
}

static int simple_lbr_stop(struct file *file)
{
    int ret = 0;

    mutex_lock(&lbr_dev.ctrl_lock);
    ret = check_owner(file);
    if (ret)
        goto out;
    simple_lbr_stop_locked();
out:
    mutex_unlock(&lbr_dev.ctrl_lock);
    return ret;
}

static int simple_lbr_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct simple_lbr_cpu_runtime *st;
    unsigned long len = vma->vm_end - vma->vm_start;
    unsigned long map_size = simple_lbr_map_size();
    unsigned long pages_per_buf = map_size >> PAGE_SHIFT;
    unsigned long cpu;

    if (check_owner(file))
        return -EBUSY;

    if (len != map_size)
        return -EINVAL;
    if (vma->vm_flags & VM_WRITE)
        return -EPERM;
    if (!pages_per_buf)
        return -EINVAL;
    if (vma->vm_pgoff % pages_per_buf)
        return -EINVAL;
    cpu = vma->vm_pgoff / pages_per_buf;
    if (cpu >= nr_cpu_ids || !cpu_possible(cpu))
        return -EINVAL;
    st = &per_cpu(cpu_state, cpu);
    if (!st->records)
        return -EINVAL;

    return remap_pfn_range(vma, vma->vm_start,
                           __pa(st->records) >> PAGE_SHIFT,
                           map_size,
                           vma->vm_page_prot);
}

static int simple_lbr_open(struct inode *inode, struct file *file)
{
    struct simple_lbr_file_ctx *ctx;
    ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
    if (!ctx)
        return -ENOMEM;
    ctx->pidns = get_pid_ns(task_active_pid_ns(current));
    file->private_data = ctx;
    return 0;
}

static int simple_lbr_release(struct inode *inode, struct file *file)
{
    struct simple_lbr_file_ctx *ctx = file->private_data;

    mutex_lock(&lbr_dev.ctrl_lock);
    if (lbr_dev.owner == file) {
        simple_lbr_stop_locked();
        clear_target_pid_locked();
        lbr_dev.owner = NULL;
    }
    mutex_unlock(&lbr_dev.ctrl_lock);

    if (ctx) {
        if (ctx->pidns)
            put_pid_ns(ctx->pidns);
        kfree(ctx);
        file->private_data = NULL;
    }
    return 0;
}

static long simple_lbr_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct simple_lbr_file_ctx *ctx = file->private_data;
    int id, ret = 0;

    if (!ctx)
        return -EINVAL;

    switch (cmd) {
    case SIMPLE_LBR_SET_PID:
        id = (int)arg;
        if (id <= 0)
            return -EINVAL;
        mutex_lock(&lbr_dev.ctrl_lock);
        ret = acquire_owner_locked(file);
        if (ret)
            goto out_unlock_setpid;
        if (READ_ONCE(lbr_dev.running)) {
            ret = -EBUSY;  /* target pid cannot be changed during collection */
            goto out_unlock_setpid;
        }
        clear_target_pid_locked();
        lbr_dev.pidns = get_pid_ns(ctx->pidns);
        lbr_dev.target_pid = id;
out_unlock_setpid:
        mutex_unlock(&lbr_dev.ctrl_lock);
        return ret;

    case SIMPLE_LBR_START:
        return simple_lbr_start(file);

    case SIMPLE_LBR_STOP:
        return simple_lbr_stop(file);

    case SIMPLE_LBR_RESET:
        mutex_lock(&lbr_dev.ctrl_lock);
        ret = check_owner(file);
        if (ret) {
            mutex_unlock(&lbr_dev.ctrl_lock);
            return ret;
        }
        if (READ_ONCE(lbr_dev.running)) {
            mutex_unlock(&lbr_dev.ctrl_lock);
            return -EBUSY;
        }
        reset_buffer_locked();
        mutex_unlock(&lbr_dev.ctrl_lock);
        return 0;

    case SIMPLE_LBR_GET_SIZE: {
        int sz = (int)simple_lbr_map_size();
        return put_user(sz, (int __user *)arg);
    }

    case SIMPLE_LBR_GET_NR_CPUS:
        return put_user(nr_cpu_ids, (int __user *)arg);

    case SIMPLE_LBR_GET_CPU_STATUS: {
        struct simple_lbr_cpu_stats st_user;
        struct simple_lbr_cpu_runtime *st;
        ret = check_owner(file);
        if (ret)
            return ret;
        if (copy_from_user(&st_user, (void __user *)arg, sizeof(st_user)))
            return -EFAULT;
        if (st_user.cpu >= nr_cpu_ids || !cpu_possible(st_user.cpu))
            return -EINVAL;
        st = &per_cpu(cpu_state, st_user.cpu);
        if (!st->records)
            return -EINVAL;
        st_user.head_seq = smp_load_acquire(&st->head_seq);
        st_user.tail_seq = READ_ONCE(st->valid_tail_seq);
        st_user.overwrite_records = READ_ONCE(st->overwrite_records);
        st_user.enable_failures = READ_ONCE(st->enable_failures);
        if (copy_to_user((void __user *)arg, &st_user, sizeof(st_user)))
            return -EFAULT;
        return 0;
    }

    case SIMPLE_LBR_ADVANCE_TAIL: {
        struct simple_lbr_tail_advance adv;
        struct simple_lbr_cpu_runtime *st;
        u64 head, valid_tail, old_consumed, new_consumed;
        ret = check_owner(file);
        if (ret)
            return ret;
        if (copy_from_user(&adv, (void __user *)arg, sizeof(adv)))
            return -EFAULT;
        if (adv.cpu >= nr_cpu_ids || !cpu_possible(adv.cpu))
            return -EINVAL;
        st = &per_cpu(cpu_state, adv.cpu);
        if (!st->records)
            return -EINVAL;
        head = smp_load_acquire(&st->head_seq);
        valid_tail = READ_ONCE(st->valid_tail_seq);
        old_consumed = READ_ONCE(st->consumed_seq);
        new_consumed = adv.tail_seq;
        if (new_consumed < valid_tail)
            new_consumed = valid_tail;
        if (new_consumed < old_consumed || new_consumed > head)
            return -EINVAL;
        smp_store_release(&st->consumed_seq, new_consumed);
        return 0;
    }
    default:
        return -ENOTTY;
    }
}

static unsigned int simple_lbr_poll(struct file *file, poll_table *wait)
{
    unsigned int mask = 0;
    int cpu;

    if (READ_ONCE(lbr_dev.owner) && READ_ONCE(lbr_dev.owner) != file)
        return 0;
    poll_wait(file, &lbr_dev.waitq, wait);

    for_each_possible_cpu(cpu) {
        struct simple_lbr_cpu_runtime *st = &per_cpu(cpu_state, cpu);
        u64 head, consumed;
        if (!st->records)
            continue;
        head = smp_load_acquire(&st->head_seq);
        consumed = READ_ONCE(st->consumed_seq);
        if (head != consumed) {
            mask = POLLIN | POLLRDNORM;
            break;
        }
    }
    return mask;
}

static const struct file_operations simple_lbr_fops = {
    .owner = THIS_MODULE,
    .open = simple_lbr_open,
    .release = simple_lbr_release,
    .mmap = simple_lbr_mmap,
    .poll = simple_lbr_poll,
    .unlocked_ioctl = simple_lbr_ioctl,
    .llseek = noop_llseek,
};

static struct miscdevice simple_lbr_miscdev = {
    MISC_DYNAMIC_MINOR,
    "simple-lbr-trace",
    &simple_lbr_fops
};

static int __init simple_lbr_init(void)
{
    int err;

    memset(&lbr_dev, 0, sizeof(lbr_dev));
    init_waitqueue_head(&lbr_dev.waitq);
    mutex_init(&lbr_dev.ctrl_lock);

    err = allocate_buffer();
    if (err)
        return err;

    err = check_arch_lbr();
    if (err) {
        free_buffer();
        return err;
    }

    err = misc_register(&simple_lbr_miscdev);
    if (err) {
        free_buffer();
        return err;
    }

    pr_info("Initialized: device=/dev/simple-lbr-trace, sample period us=%u\n", sample_period);
    return 0;
}
module_init(simple_lbr_init);

static void __exit simple_lbr_exit(void)
{
    mutex_lock(&lbr_dev.ctrl_lock);
    simple_lbr_stop_locked();
    clear_target_pid_locked();
    WRITE_ONCE(lbr_dev.owner, NULL);
    mutex_unlock(&lbr_dev.ctrl_lock);

    misc_deregister(&simple_lbr_miscdev);
    free_buffer();

    pr_info("simple-trace-lbr: Exited\n");
}
module_exit(simple_lbr_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("HW");
MODULE_DESCRIPTION("Arch LBR collector");