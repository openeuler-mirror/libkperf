#!/usr/bin/env python3
import re
import sys
import math
from collections import defaultdict

LINE_RE = re.compile(
    r'^\[(\d+):(\d+):(\d+)\.(\d+)\]\s+'
    r'.*?\[(\d+):(\d+)\]\s+'
    r'(->|<-)\s+'
    r'(.*?)::([^\s]+)\s+@'
)

def timestamp_to_ns(hour, minute, second, fraction):
    fraction = fraction.ljust(9, '0')[:9]
    return (
        int(hour) * 3600 * 1_000_000_000
        + int(minute) * 60 * 1_000_000_000
        + int(second) * 1_000_000_000
        + int(fraction)
    )

def percentile_nearest_rank(values, percentile):
    if not values:
        return 0
    values = sorted(values)
    rank = math.ceil(percentile / 100.0 * len(values))
    return values[max(0, min(rank - 1, len(values) - 1))]

def parse_trace(filename):
    call_stacks = defaultdict(list)
    durations = defaultdict(list)

    unmatched_enter = 0
    unmatched_exit = 0
    unparsed = 0

    with open(filename, "r", encoding="utf-8", errors="replace") as f:
        for line_no, line in enumerate(f, 1):
            line = line.strip()
            match = LINE_RE.match(line)

            if not match:
                if "->" in line or "<-" in line:
                    print(f"[UNPARSED] line={line_no}: {line}",
                          file=sys.stderr)
                    unparsed += 1
                continue

            (
                hour,
                minute,
                second,
                fraction,
                tid,
                cpu,
                direction,
                module,
                function,
            ) = match.groups()

            timestamp_ns = timestamp_to_ns(
                hour, minute, second, fraction
            )

            tid = int(tid)
            cpu = int(cpu)
            module = module.strip()

            # CPU 不参与匹配
            key = (tid, module, function)

            if direction == "->":
                call_stacks[key].append(
                    (timestamp_ns, line_no, cpu)
                )
                continue

            if not call_stacks[key]:
                unmatched_exit += 1
                print(
                    f"[UNMATCHED EXIT] line={line_no} "
                    f"tid={tid} cpu={cpu} "
                    f"{module}::{function}",
                    file=sys.stderr
                )
                continue

            enter_ns, enter_line, enter_cpu = call_stacks[key].pop()
            duration_ns = timestamp_ns - enter_ns

            if duration_ns < 0:
                print(
                    f"[NEGATIVE] enter_line={enter_line} "
                    f"exit_line={line_no} "
                    f"{module}::{function}",
                    file=sys.stderr
                )
                continue

            durations[(module, function)].append(duration_ns)

    for (tid, module, function), stack in call_stacks.items():
        for _, line_no, cpu in stack:
            unmatched_enter += 1
            print(
                f"[UNMATCHED ENTER] line={line_no} "
                f"tid={tid} cpu={cpu} "
                f"{module}::{function}",
                file=sys.stderr
            )

    return durations, unmatched_enter, unmatched_exit, unparsed

def ns_to_ms(ns):
    return ns / 1_000_000.0

def print_statistics(durations):
    rows = []

    for (module, function), values in durations.items():
        count = len(values)
        avg_ns = sum(values) / count
        min_ns = min(values)
        max_ns = max(values)
        p99_ns = percentile_nearest_rank(values, 99)

        rows.append((
            function,
            module,
            count,
            avg_ns,
            min_ns,
            max_ns,
            p99_ns
        ))

    rows.sort(key=lambda x: x[3], reverse=True)

    print(
        f"{'Function':<50} "
        f"{'Count':>8} "
        f"{'Avg(ms)':>12} "
        f"{'Min(ms)':>12} "
        f"{'Max(ms)':>12} "
        f"{'P99(ms)':>12} "
        f"Module"
    )

    print("-" * 150)

    for function, module, count, avg_ns, min_ns, max_ns, p99_ns in rows:
        print(
            f"{function:<50} "
            f"{count:>8} "
            f"{ns_to_ms(avg_ns):>12.6f} "
            f"{ns_to_ms(min_ns):>12.6f} "
            f"{ns_to_ms(max_ns):>12.6f} "
            f"{ns_to_ms(p99_ns):>12.6f} "
            f"{module}"
        )

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} trace.txt", file=sys.stderr)
        sys.exit(1)

    durations, unmatched_enter, unmatched_exit, unparsed = \
        parse_trace(sys.argv[1])

    print_statistics(durations)

    print()
    print(f"Unmatched enter : {unmatched_enter}")
    print(f"Unmatched exit  : {unmatched_exit}")
    print(f"Unparsed events : {unparsed}")

if __name__ == "__main__":
    main()