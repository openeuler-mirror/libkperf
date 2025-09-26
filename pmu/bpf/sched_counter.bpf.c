// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
// Copyright (c) 2021 Facebook

#include <bpf/vmlinux.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>

char LICENSE[] SEC("license") = "Dual BSD/GPL";

#define MAX_ENTRIES 102400

// system pmu count. key: pid, value : count of each core
struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, sizeof(int));
    __uint(map_flags, BPF_F_PRESERVE_ELEMS);
} events SEC(".maps");

// system pmu count at last time sched_switch was triggered
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, sizeof(struct bpf_perf_event_value));
    __uint(max_entries, 1);
} prev_readings SEC(".maps");

// accumulated pmu count of pid. key: accum_key, value: count of each core
// If a pid creates a child process/thread, they use the same accum key as this pid and their pmu events accumulated it
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_HASH);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, sizeof(struct bpf_perf_event_value));
    __uint(max_entries, 1024);
} accum_readings SEC(".maps");

// check whether to record pmu value. key: pid, value: accum_key
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, sizeof(__u32));
    __uint(max_entries, MAX_ENTRIES);
    __uint(map_flags, BPF_F_NO_PREALLOC);
} filter SEC(".maps");

SEC("raw_tp/sched_switch")
int BPF_PROG(on_switch)
{
    __u32 pid;
    __u32 zero=0;
    __u32 *accum_key;
    __u32 cpu = bpf_get_smp_processor_id();
    long err;
    struct bpf_perf_event_value cur_val, *prev_val, *accum_val;

    prev_val = bpf_map_lookup_elem(&prev_readings, &zero);
    if (!prev_val) {
        bpf_printk("failed to bpf_map_lookup_elem prev_readings.\n");
        return 0;
    }

    // get pmu value by API of bpf
    err = bpf_perf_event_read_value(&events, BPF_F_CURRENT_CPU, &cur_val, sizeof(struct bpf_perf_event_value));
    if (err) {
         bpf_printk("failed to bpf_event_read_value: %d cpu %d\n", err, cpu);
        return 0;
    }
    pid = bpf_get_current_pid_tgid() & 0xffffffff;
    accum_key = bpf_map_lookup_elem(&filter, &pid);
    if (!accum_key) {
        return 0;
    }

    accum_val = bpf_map_lookup_elem(&accum_readings, accum_key);
    if (!accum_val) {
        *prev_val = cur_val;
        return 0;
    }

    accum_val->counter += cur_val.counter - prev_val->counter;
    accum_val->enabled += cur_val.enabled - prev_val->enabled;
    accum_val->running += cur_val.running - prev_val->running;
    bpf_printk("cur_val counting: %ld prev_val counting: %ld accum_val counting: %ld\n", 
                cur_val.counter, prev_val->counter, accum_val->counter);

    *prev_val = cur_val;
    return 0;
}

SEC("tp_btf/task_newtask")
int BPF_PROG(on_newtask, struct task_struct *task, __u64 clone_flags)
{
    long err;
    __u32 new_pid;
    __u32 parent_pid;
    __u32 *accum_key;
    struct bpf_perf_event_value *accum_val;

    parent_pid = bpf_get_current_pid_tgid() & 0xffffffff;
    new_pid = task->pid;

    accum_key = bpf_map_lookup_elem(&filter, &parent_pid);
    if (!accum_key) {
        return 0;
    }

    bpf_map_update_elem(&filter, &new_pid, accum_key, BPF_NOEXIST);
    bpf_printk("new pid: %d parent: %d accum_key: %ld\n", new_pid, parent_pid, *accum_key);
    return 0;
}