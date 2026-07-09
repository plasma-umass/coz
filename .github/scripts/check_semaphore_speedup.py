#!/usr/bin/env python3
"""Assert that coz measures a real speedup for benchmarks/sem_toy.

sem_toy's main thread joins its workers on a semaphore. A thread blocked on a
semaphore is not running, so it must not be charged for the virtual delays
inserted while it slept. When libcoz does not interpose the semaphore, the main
thread -- the one that visits the progress point -- pays them all on wake-up,
the measured period grows with the speedup, and the slope collapses to zero or
goes negative.

Both of sem_toy's loops inline the same xorshift, so the hot line's slope should
approach 1.0: removing that work removes the program.
"""
import json
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <profile.jsonl>")
        return 2

    rows, current = [], None
    with open(sys.argv[1]) as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            record = json.loads(line)
            if record["type"] == "experiment":
                current = record
            elif record["type"] == "throughput-point" and current is not None:
                current["delta"] = record["delta"]
                rows.append(current)
                current = None

    if not rows:
        print("ERROR: no experiments in profile")
        return 1

    # A duration past 2^63 means the inserted delay exceeded the experiment's
    # wall time and the unsigned subtraction wrapped.
    underflowed = [r for r in rows if r["duration"] > 10**12]
    if underflowed:
        print(f"ERROR: {len(underflowed)} experiment(s) with underflowed duration")
        return 1

    periods = {}
    for row in rows:
        if row["delta"]:
            periods.setdefault(round(row["speedup"], 2), []).append(row["duration"] / row["delta"])

    baseline = periods.get(0.0)
    if not baseline:
        print("WARNING: no 0% baseline experiments; skipping slope check")
        return 0

    best = max(periods)
    if best < 0.5:
        print(f"WARNING: largest speedup sampled was {best:.0%}; skipping slope check")
        return 0

    p0 = sum(baseline) / len(baseline)
    ps = sum(periods[best]) / len(periods[best])
    improvement = 1 - ps / p0

    print(f"baseline period {p0:.0f}ns; period at {best:.0%} speedup {ps:.0f}ns")
    print(f"measured improvement: {improvement:.1%}")

    if improvement < 0.25:
        print("ERROR: virtual speedup produced no throughput gain -- is the "
              "blocked main thread being charged for delays it never paid?")
        return 1

    print("Semaphore interposition validated")
    return 0


if __name__ == "__main__":
    sys.exit(main())
