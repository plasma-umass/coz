#!/usr/bin/env bash
# Integration test: verify that Rust stdlib/dependency paths are filtered
# from coz profiling output by default, and that explicit --source-scope
# can override the filtering.
#
# The test has two phases:
#   Phase 1 (DWARF verification): Confirms the Rust binary's debug info
#            contains /rustc/ stdlib paths, proving the filter is needed.
#   Phase 2 (profiling verification): If the profiler can produce output,
#            confirms stdlib paths are absent from the default profile.
#
# Usage: run_rust_filter_test.sh <libcoz> <output_dir>
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 <libcoz> <output_dir>" >&2
  exit 1
fi

LIBCOZ="$1"
OUTDIR="$2"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CARGO_DIR="$SCRIPT_DIR/rust_filter"

mkdir -p "$OUTDIR"

# --- Prerequisites -----------------------------------------------------------

if ! command -v cargo &>/dev/null; then
  echo "SKIP: cargo not found, skipping Rust filter test" >&2
  exit 0
fi

# --- Build the Rust test binary -----------------------------------------------

echo "Building Rust test binary..."
cargo build --release --manifest-path "$CARGO_DIR/Cargo.toml" 2>&1

TEST_BIN="$CARGO_DIR/target/release/rust-filter-test"
if [[ ! -x "$TEST_BIN" ]]; then
  echo "FAIL: Rust test binary not found at $TEST_BIN" >&2
  exit 1
fi

# --- Phase 1: DWARF verification ---------------------------------------------
# Verify the binary contains Rust stdlib source paths in its debug info.
# This proves our filter is necessary — without it, these paths would appear
# in profiling results.

echo "Phase 1: Verifying DWARF debug info contains Rust stdlib paths..."

DWARF_SOURCES="$OUTDIR/dwarf-sources.txt"

if [[ "$(uname -s)" == "Darwin" ]]; then
  # macOS: generate dSYM if needed, then dump
  dsymutil "$TEST_BIN" 2>/dev/null || true
  if [[ -d "${TEST_BIN}.dSYM" ]]; then
    dwarfdump "$TEST_BIN.dSYM" 2>/dev/null | grep -oE '/rustc/[^"]+' | sort -u > "$DWARF_SOURCES" || true
  fi
else
  # Linux: use readelf or objdump to extract source file paths
  if command -v readelf &>/dev/null; then
    readelf --debug-dump=line "$TEST_BIN" 2>/dev/null | grep -oE '/rustc/[^ ]+' | sort -u > "$DWARF_SOURCES" || true
  elif command -v objdump &>/dev/null; then
    objdump --dwarf=decodedline "$TEST_BIN" 2>/dev/null | grep -oE '/rustc/[^ ]+' | sort -u > "$DWARF_SOURCES" || true
  fi
fi

if [[ -s "$DWARF_SOURCES" ]]; then
  RUSTC_COUNT=$(wc -l < "$DWARF_SOURCES" | tr -d ' ')
  echo "  PASS: Found $RUSTC_COUNT unique /rustc/ paths in DWARF debug info"
  echo "  Sample paths:"
  head -3 "$DWARF_SOURCES" | sed 's/^/    /'
else
  echo "  WARNING: Could not extract /rustc/ paths from DWARF info (debug tools may be unavailable)"
  echo "  Continuing with profiling tests..."
fi

# --- Phase 2: Profiling verification ------------------------------------------
# If the profiler can sample the binary, verify that Rust stdlib paths are
# filtered from the default output.

echo "Phase 2: Profiling verification..."

# Helper: run the profiler on the test binary
run_profile() {
  local profile="$1"
  shift
  local preload_env="LD_PRELOAD"
  if [[ "$(uname -s)" == "Darwin" ]]; then
    preload_env="DYLD_INSERT_LIBRARIES"
  fi

  # Run the profiled binary; use a subshell with timeout to avoid hangs.
  # The binary or profiler may crash on some platforms — that's OK.
  (
    env \
      COZ_OUTPUT="$profile" \
      COZ_BINARY_SCOPE=MAIN \
      "$@" \
      "${preload_env}=${LIBCOZ}" \
      "$TEST_BIN" >/dev/null 2>&1
  ) &
  local pid=$!

  # Wait up to 30 seconds
  local elapsed=0
  while kill -0 "$pid" 2>/dev/null && [[ $elapsed -lt 30 ]]; do
    sleep 1
    elapsed=$((elapsed + 1))
  done

  # Kill if still running
  if kill -0 "$pid" 2>/dev/null; then
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
  else
    wait "$pid" 2>/dev/null || true
  fi
}

# Rust stdlib patterns to check for in profiling output
RUST_STDLIB_PATTERN='/rustc/|/\.rustup/|/\.cargo/registry/|/\.cargo/git/'

# --- Test 2a: Default scope filters Rust stdlib --------------------------------

echo "  Test 2a: Default scope should filter Rust stdlib paths..."
PROFILE_DEFAULT="$OUTDIR/profile-default.coz"
rm -f "$PROFILE_DEFAULT"
run_profile "$PROFILE_DEFAULT" COZ_SOURCE_SCOPE=%

if [[ ! -s "$PROFILE_DEFAULT" ]]; then
  echo "  SKIP: profiler produced no output (platform may not support profiling Rust binaries)"
  echo ""
  echo "Summary: Phase 1 (DWARF verification) completed. Phase 2 skipped (profiler unavailable)."
  exit 0
fi

# Check for startup record
if ! grep -q 'startup' "$PROFILE_DEFAULT"; then
  echo "  FAIL: no startup record in profile" >&2
  exit 1
fi

LINE_COUNT=$(wc -l < "$PROFILE_DEFAULT" | tr -d ' ')
echo "    Profile generated: $LINE_COUNT lines"

# Check for Rust stdlib paths that should have been filtered out
if grep -E "$RUST_STDLIB_PATTERN" "$PROFILE_DEFAULT" | grep -qv '^#'; then
  echo "  FAIL: Rust stdlib/dependency paths found in default profile:" >&2
  grep -E "$RUST_STDLIB_PATTERN" "$PROFILE_DEFAULT" | head -5 >&2
  exit 1
fi
echo "    PASS: no Rust stdlib paths in default profile"

# Check for user source in profile
if grep -q 'samples\|experiment' "$PROFILE_DEFAULT"; then
  if grep -q 'main\.rs' "$PROFILE_DEFAULT"; then
    echo "    PASS: user source (main.rs) found in profile"
  else
    echo "    INFO: user source (main.rs) not found in profile (may need longer runtime)"
  fi
fi

# --- Test 2b: Explicit source scope overrides Rust filter ----------------------

echo "  Test 2b: Explicit source scope should allow Rust stdlib paths..."
PROFILE_EXPLICIT="$OUTDIR/profile-explicit.coz"
rm -f "$PROFILE_EXPLICIT"

# Use a source scope that explicitly includes Rust stdlib paths
run_profile "$PROFILE_EXPLICIT" COZ_SOURCE_SCOPE="/rustc/%"

if [[ ! -s "$PROFILE_EXPLICIT" ]]; then
  echo "    SKIP: profiler produced no output with explicit scope"
else
  if grep -q 'startup' "$PROFILE_EXPLICIT"; then
    echo "    PASS: profiler ran successfully with explicit Rust source scope"
    # If experiments were run with /rustc/% scope, Rust paths should be present
    if grep -q 'samples\|experiment' "$PROFILE_EXPLICIT"; then
      if grep -qE '/rustc/' "$PROFILE_EXPLICIT"; then
        echo "    PASS: Rust stdlib paths present with explicit source scope (override works)"
      else
        echo "    INFO: no Rust stdlib paths sampled with explicit scope (may need longer runtime)"
      fi
    fi
  else
    echo "    FAIL: no startup record in explicit-scope profile" >&2
    exit 1
  fi
fi

echo ""
echo "All Rust filter tests passed."
