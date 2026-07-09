#!/usr/bin/env python3
"""Assert that a coz profile contains experiments attributed to a source file.

Usage: check_profile.py <profile.jsonl> <expected-source-substring>

Used by the per-language CI jobs. A binding is only "working" if libcoz both
registered its progress points and attributed experiments back to that
language's source, so this checks for experiment records naming the file.
"""
import collections
import json
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <profile.jsonl> <expected-source-substring>")
        return 2

    path, expected = sys.argv[1], sys.argv[2]

    records = []
    with open(path) as handle:
        for line in handle:
            line = line.strip()
            if line:
                records.append(json.loads(line))

    kinds = collections.Counter(r["type"] for r in records)
    print(f"record types: {dict(kinds)}")

    experiments = [r for r in records if r["type"] == "experiment"]
    if not experiments:
        print("ERROR: profile contains no experiments")
        return 1

    matching = [e for e in experiments if expected in e.get("selected", "")]
    selected = collections.Counter(e.get("selected", "") for e in experiments)
    print(f"experiments: {len(experiments)} ({len(matching)} naming {expected!r})")
    for location, count in selected.most_common(10):
        print(f"  {count:4d}  {location}")

    if not matching:
        print(f"ERROR: no experiment selected a line in {expected!r}")
        return 1

    print(f"OK: {len(matching)} experiments attributed to {expected!r}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
