/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 * libkperf licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: Wu
 * Create: 2025-08-10
 * Description: the bpf program for cgroup collecting in counting mode
 ******************************************************************************/
#include <bpf/vmlinux.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>

char LICENSE[] SEC("license") = "Dual BSD/GPL";

#define MAX_LEVELS  10      // max cgroup hierarchy level: arbitrary
#define MAX_EVENTS  128     // max events per cgroup: arbitrary
#define MAX_ENTRIES 102400

// single set of global perf events to measure
// {evt0, cpu0}, {evt0, cpu1}, {evt0, cpu2}...{evt0, cpuM}, {evt1, cpu0}...{evtM, cpuM}
struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, sizeof(int));
    __uint(map_flags, BPF_F_PRESERVE_ELEMS);
} events SEC(".maps");

// from cgroup id to event index
// key: cgroup id from OS
// value: internal id from 0...M
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(key_size, sizeof(__u64));
    __uint(value_size, sizeof(__u32));
    __uint(max_entries, MAX_ENTRIES);
} cgrp_idx SEC(".maps");

// per-cpu event snapshots to calculate delta
// {evt0}, {evt1}...{evtM}
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, sizeof(struct bpf_perf_event_value));
} prev_readings SEC(".maps");

// aggregated event values for each cgroup (per-cpu)
// will be read from the user-space
// {cgrp0, evt0, cpu0}, {cgrp0, evt0, cpu1}...{cgrp0, evt0, cpuM}, {cgrp0, evt1, cpu0}...{cgrp0, evtM, cpuM}...{cgrpM, evtM, cpuM}
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, sizeof(struct bpf_perf_event_value));
} cgrp_readings SEC(".maps");

const volatile __u32 num_events = 1;
const volatile __u32 num_cpus = 1;

static inline int get_cgroup_idx(__u32 *cgrps, int size)
{
    struct task_struct *p = (void *)bpf_get_current_task();
    struct cgroup *cgrp;
    register int i = 0;
    __u32 *elem;
    int level;
    int cnt;

    cgrp = BPF_CORE_READ(p, cgroups, subsys[perf_event_cgrp_id], cgroup);
    level = BPF_CORE_READ(cgrp, level);

    for (cnt = 0; i < MAX_LEVELS; i++) {
        __u64 cgrp_id;

        if (i > level) {
            break;
        }

        // convert cgroup-id to a map index
        if (bpf_core_field_exists(cgrp->ancestor_ids)) {
            cgrp_id = BPF_CORE_READ(cgrp, ancestor_ids[i]);
        } else {
            bpf_printk("cannot get ancestor_ids, this field not in struct cgroup");
            return 0;
        }
        elem = bpf_map_lookup_elem(&cgrp_idx, &cgrp_id);
        if (!elem) {
            continue;
        }

        cgrps[cnt++] = *elem;
        if (cnt == size) {
            break;
        }
    }

    return cnt;
}

static int bperf_cgroup_count(void)
{
    register __u32 idx = 0;  // to have it in a register to pass BPF verifier
    register int c = 0;
    struct bpf_perf_event_value val, delta, *prev_val, *cgrp_val;
    __u32 cpu = bpf_get_smp_processor_id();
    __u32 cgrp_idx[MAX_LEVELS];
    int cgrp_cnt;
    __u32 key, cgrp;
    long err;

    cgrp_cnt = get_cgroup_idx(cgrp_idx, MAX_LEVELS);

    for (; idx < MAX_EVENTS; idx++) {
        if (idx == num_events)
            break;

        // XXX: do not pass idx directly (for verifier)
        key = idx;
        // this is per-cpu array for diff
        prev_val = bpf_map_lookup_elem(&prev_readings, &key);
        if (!prev_val) {
            val.counter = val.enabled = val.running = 0;
            bpf_map_update_elem(&prev_readings, &key, &val, BPF_ANY);

            prev_val = bpf_map_lookup_elem(&prev_readings, &key);
            if (!prev_val) {
                return 0;
            }
        }

        // read from global perf_event array
        key = idx * num_cpus + cpu;
        err = bpf_perf_event_read_value(&events, key, &val, sizeof(val));
        if (err) {
            bpf_printk("bpf_perf_event_read_value failed, continue");
            continue;
        }

        delta.counter = val.counter - prev_val->counter;
        delta.enabled = val.enabled - prev_val->enabled;
        delta.running = val.running - prev_val->running;
        bpf_printk("prev_val : %ld val : %ld delta : %ld \n", prev_val->counter, val.counter, delta.counter);

        for (c = 0; c < MAX_LEVELS; c++) {
            if (c == cgrp_cnt)
                break;
            cgrp = cgrp_idx[c];

            // aggregate the result by cgroup
            key = cgrp * num_events + idx;
            cgrp_val = bpf_map_lookup_elem(&cgrp_readings, &key);
            if (cgrp_val) {
                cgrp_val->counter += delta.counter;
                cgrp_val->enabled += delta.enabled;
                cgrp_val->running += delta.running;
                bpf_printk("cgrp_val : %ld\n", cgrp_val->counter);
            } else {
                bpf_map_update_elem(&cgrp_readings, &key, &delta, BPF_ANY);
            }
        }

        *prev_val = val;
    }
    return 0;
}

SEC("raw_tp/sched_switch")
int BPF_PROG(trigger_read)
{
    return bperf_cgroup_count();
}
