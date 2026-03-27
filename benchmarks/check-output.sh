#!/bin/sh

set -e
rm -f profile.jsonl

$@

grep -q '"time":' profile.jsonl || { echo failure: valid profile.jsonl not generated; exit 1; }
grep -q '"throughput-point"' profile.jsonl || { echo failure: throughput-point not found in profile; exit 1; }
grep -q -P '{"type":"samples","location":' profile.jsonl || { echo failure: samples not found in profile; exit 1; }
echo success: benchmark generated valid profile.jsonl
