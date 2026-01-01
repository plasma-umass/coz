#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "usage: $0 <libcoz.so> <binary> <output_dir>" >&2
  exit 1
fi

LIBCOZ="$1"
TEST_BIN="$2"
OUTDIR="$3"
mkdir -p "$OUTDIR"

run_profile() {
  local profile="$1"
  shift
  local preload_env="LD_PRELOAD"
  if [[ "$(uname -s)" == "Darwin" ]]; then
    preload_env="DYLD_INSERT_LIBRARIES"
  fi

  env \
    COZ_OUTPUT="$profile" \
    COZ_BINARY_SCOPE=MAIN \
    COZ_SOURCE_SCOPE=% \
    COZ_PROGRESS_POINTS= \
    "${preload_env}=${LIBCOZ}" \
    "$TEST_BIN" >/dev/null
}

PROFILE_DEFAULT="$OUTDIR/profile-default.coz"
run_profile "$PROFILE_DEFAULT"

grep -Eq '^samples[[:space:]]+location=.*dwarf_scope_test\.cpp' "$PROFILE_DEFAULT"
if grep -Eq '/usr/(include|lib)/' "$PROFILE_DEFAULT"; then
  echo "unexpected system source in default profile" >&2
  exit 1
fi

PROFILE_FILTERED="$OUTDIR/profile-filtered.coz"
COZ_FILTER_SYSTEM=1 run_profile "$PROFILE_FILTERED"
grep -Eq '^samples[[:space:]]+location=.*dwarf_scope_test\.cpp' "$PROFILE_FILTERED"
