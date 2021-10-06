#!/bin/sh

set -e
rm -f profile.coz

$@

grep -q "time=" profile.coz || { echo failure: valid profile.coz not generated; exit 1; }
grep -q "throughput-point" profile.coz || { echo failure: throughput-point not found in profile; exit 1; }
grep -q -P "samples\tlocation=" profile.coz || { echo failure: samples not found in profile; exit 1; }
echo success: benchmark generated valid profile.coz
