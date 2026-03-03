/**
 * Unit tests for path filtering functions in libcoz/path_filter.h.
 * Verifies that Rust toolchain/dependency paths are correctly identified,
 * system paths are recognized, and coz headers are excluded.
 */

#include "path_filter.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_set>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
  static void test_##name(); \
  static struct Register_##name { \
    Register_##name() { test_##name(); } \
  } register_##name; \
  static void test_##name()

#define ASSERT_TRUE(expr) do { \
  tests_run++; \
  if(!(expr)) { \
    fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
  } else { \
    tests_passed++; \
  } \
} while(0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

// Helper: save/restore HOME around a block
struct HomeGuard {
  std::string saved;
  bool had_home;
  HomeGuard(const char* new_home) {
    const char* h = std::getenv("HOME");
    had_home = (h != nullptr);
    if(had_home) saved = h;
    setenv("HOME", new_home, 1);
  }
  ~HomeGuard() {
    if(had_home) setenv("HOME", saved.c_str(), 1);
    else unsetenv("HOME");
  }
};

// ============================================================
// is_rust_path tests
// ============================================================

TEST(rustc_embedded_stdlib) {
  // /rustc/<hash>/library/... paths don't depend on HOME
  ASSERT_TRUE(is_rust_path("/rustc/abc123def456/library/core/src/ops/function.rs"));
  ASSERT_TRUE(is_rust_path("/rustc/90b35a6239c3d8bdabc530a6a0816f7ff89a0aaf/library/std/src/io/mod.rs"));
  ASSERT_TRUE(is_rust_path("/rustc/hash/library/alloc/src/vec/mod.rs"));
}

TEST(rustup_toolchain) {
  // Rust stdlib installed via rustup — only matched when under $HOME
  HomeGuard hg("/home/user");
  ASSERT_TRUE(is_rust_path("/home/user/.rustup/toolchains/stable-x86_64-unknown-linux-gnu/lib/rustlib/src/rust/library/core/src/ptr/mod.rs"));
  ASSERT_TRUE(is_rust_path("/home/user/.rustup/toolchains/nightly-x86_64-unknown-linux-gnu/lib/rustlib/src/rust/library/std/src/sync/mutex.rs"));
}

TEST(rustup_macos) {
  HomeGuard hg("/Users/dev");
  ASSERT_TRUE(is_rust_path("/Users/dev/.rustup/toolchains/stable-aarch64-apple-darwin/lib/rustlib/src/rust/library/alloc/src/string.rs"));
}

TEST(cargo_registry_deps) {
  // Third-party crates from crates.io
  HomeGuard hg("/home/user");
  ASSERT_TRUE(is_rust_path("/home/user/.cargo/registry/src/index.crates.io-6f17d22bba15001f/tokio-1.28.0/src/runtime/scheduler/multi_thread/worker.rs"));
  ASSERT_TRUE(is_rust_path("/home/user/.cargo/registry/src/github.com-1ecc6299db9ec823/serde-1.0.160/src/de/mod.rs"));
}

TEST(cargo_git_deps) {
  HomeGuard hg("/home/user");
  ASSERT_TRUE(is_rust_path("/home/user/.cargo/git/checkouts/some-crate-abc123/def456/src/lib.rs"));
}

TEST(rustup_wrong_home_not_matched) {
  // .rustup under a *different* user's home should NOT match
  HomeGuard hg("/home/alice");
  ASSERT_FALSE(is_rust_path("/home/bob/.rustup/toolchains/stable/lib/file.rs"));
  ASSERT_FALSE(is_rust_path("/home/bob/.cargo/registry/src/tokio/src/lib.rs"));
  ASSERT_FALSE(is_rust_path("/home/bob/.cargo/git/checkouts/crate/src/lib.rs"));
}

TEST(rustup_arbitrary_path_not_matched) {
  // .rustup appearing in a non-home location should NOT match
  HomeGuard hg("/home/user");
  ASSERT_FALSE(is_rust_path("/srv/builds/.rustup/toolchains/stable/lib/file.rs"));
  ASSERT_FALSE(is_rust_path("/opt/ci/.cargo/registry/src/tokio/src/lib.rs"));
  ASSERT_FALSE(is_rust_path("/tmp/.cargo/git/checkouts/crate/src/lib.rs"));
}

TEST(user_code_not_rust_path) {
  HomeGuard hg("/home/user");
  ASSERT_FALSE(is_rust_path("/home/user/projects/myapp/src/main.rs"));
  ASSERT_FALSE(is_rust_path("/home/user/projects/myapp/src/lib.rs"));
  ASSERT_FALSE(is_rust_path("/opt/myapp/src/server.rs"));
  ASSERT_FALSE(is_rust_path("/home/user/git/myproject/src/main.cpp"));
}

TEST(user_code_with_rust_in_name) {
  HomeGuard hg("/home/user");
  ASSERT_FALSE(is_rust_path("/home/user/rust-project/src/main.rs"));
  ASSERT_FALSE(is_rust_path("/home/user/projects/rusty-app/src/lib.rs"));
}

TEST(cargo_project_not_filtered) {
  // User's own .cargo/bin, .cargo/config should not be filtered
  HomeGuard hg("/home/user");
  ASSERT_FALSE(is_rust_path("/home/user/.cargo/bin/myapp"));
  ASSERT_FALSE(is_rust_path("/home/user/.cargo/config.toml"));
}

TEST(rustc_prefix_boundary) {
  // /rustc must be a proper path prefix, not just a substring
  ASSERT_TRUE(is_rust_path("/rustc/hash/file.rs"));
  ASSERT_TRUE(is_rust_path("/rustc"));
  ASSERT_FALSE(is_rust_path("/rustcompiler/src/main.rs"));
}

TEST(no_home_set) {
  // When HOME is not set, only /rustc/ paths should match
  const char* orig = std::getenv("HOME");
  unsetenv("HOME");
  ASSERT_TRUE(is_rust_path("/rustc/hash/library/core/src/ptr/mod.rs"));
  ASSERT_FALSE(is_rust_path("/home/user/.rustup/toolchains/stable/lib/file.rs"));
  ASSERT_FALSE(is_rust_path("/home/user/.cargo/registry/src/tokio/src/lib.rs"));
  if(orig) setenv("HOME", orig, 1);
}

// ============================================================
// is_system_path tests
// ============================================================

TEST(system_paths_detected) {
  ASSERT_TRUE(is_system_path("/usr/include/stdio.h"));
  ASSERT_TRUE(is_system_path("/usr/include/c++/11/string"));
  ASSERT_TRUE(is_system_path("/usr/lib/gcc/x86_64-linux-gnu/11/include/stddef.h"));
  ASSERT_TRUE(is_system_path("/usr/local/include/boost/asio.hpp"));
  ASSERT_TRUE(is_system_path("/usr/local/lib/libfoo.so"));
  ASSERT_TRUE(is_system_path("/lib/x86_64-linux-gnu/libc.so.6"));
  ASSERT_TRUE(is_system_path("/lib64/ld-linux-x86-64.so.2"));
}

TEST(user_paths_not_system) {
  ASSERT_FALSE(is_system_path("/home/user/project/src/main.cpp"));
  ASSERT_FALSE(is_system_path("/opt/app/lib/mylib.so"));
  ASSERT_FALSE(is_system_path("/tmp/build/output.o"));
}

// ============================================================
// is_coz_header tests
// ============================================================

TEST(coz_header_detected) {
  ASSERT_TRUE(is_coz_header("/usr/include/coz.h"));
  ASSERT_TRUE(is_coz_header("/home/user/git/coz/include/coz.h"));
  ASSERT_TRUE(is_coz_header("/coz.h"));
}

TEST(non_coz_header) {
  ASSERT_FALSE(is_coz_header("/usr/include/coz_utils.h"));
  ASSERT_FALSE(is_coz_header("/home/user/coz.cpp"));
  ASSERT_FALSE(is_coz_header("/home/user/mycoz.h"));
}

// ============================================================
// path_has_prefix tests
// ============================================================

TEST(prefix_matching) {
  ASSERT_TRUE(path_has_prefix("/usr/include/stdio.h", "/usr/include"));
  ASSERT_TRUE(path_has_prefix("/usr/include", "/usr/include"));
  ASSERT_FALSE(path_has_prefix("/usr/includes/foo.h", "/usr/include"));
  ASSERT_FALSE(path_has_prefix("/usr", "/usr/include"));
  ASSERT_FALSE(path_has_prefix("", "/usr"));
  ASSERT_FALSE(path_has_prefix("/usr/include", ""));
}

// ============================================================
// Scope override logic test
//
// This replicates the filtering decision from file_matches_scope
// in inspect.cpp to verify that explicit source scopes override
// Rust path filtering.
// ============================================================

// Simplified version of the scope check logic from inspect.cpp
static bool would_match_scope(const std::string& path,
                              const std::unordered_set<std::string>& scope) {
  if(is_coz_header(path))
    return false;
  if(scope.empty())
    return true;
  bool default_scope = (scope.size() == 1 && scope.count("%") == 1);
  if(default_scope && is_rust_path(path))
    return false;
  // With default scope (%), everything else matches
  // With explicit scope, we'd need wildcard_match which is in inspect.cpp
  // For testing, we just check the Rust filter logic
  return true;
}

TEST(default_scope_filters_rust) {
  HomeGuard hg("/home/user");
  std::unordered_set<std::string> default_scope = {"%"};
  // Rust paths should be filtered with default scope
  ASSERT_FALSE(would_match_scope("/rustc/hash/library/core/src/ptr/mod.rs", default_scope));
  ASSERT_FALSE(would_match_scope("/home/user/.rustup/toolchains/stable/lib/file.rs", default_scope));
  ASSERT_FALSE(would_match_scope("/home/user/.cargo/registry/src/tokio/src/lib.rs", default_scope));
  ASSERT_FALSE(would_match_scope("/home/user/.cargo/git/checkouts/crate/src/lib.rs", default_scope));
  // User code should still pass with default scope
  ASSERT_TRUE(would_match_scope("/home/user/project/src/main.rs", default_scope));
  ASSERT_TRUE(would_match_scope("/home/user/project/src/main.cpp", default_scope));
}

TEST(explicit_scope_allows_rust) {
  HomeGuard hg("/home/user");
  // An explicit scope (not just "%") should NOT filter Rust paths
  std::unordered_set<std::string> explicit_scope = {"/rustc/%"};
  ASSERT_TRUE(would_match_scope("/rustc/hash/library/core/src/ptr/mod.rs", explicit_scope));

  std::unordered_set<std::string> explicit_scope2 = {"/home/user/.cargo/%"};
  ASSERT_TRUE(would_match_scope("/home/user/.cargo/registry/src/tokio/src/lib.rs", explicit_scope2));
}

TEST(explicit_scope_with_multiple_patterns) {
  // Multiple patterns = explicit scope, should not filter Rust paths
  std::unordered_set<std::string> multi_scope = {"%", "/extra/path"};
  ASSERT_TRUE(would_match_scope("/rustc/hash/library/core/src/ptr/mod.rs", multi_scope));
}

TEST(coz_header_always_filtered) {
  // coz.h should be filtered regardless of scope
  std::unordered_set<std::string> default_scope = {"%"};
  std::unordered_set<std::string> explicit_scope = {"/usr/include/%"};
  ASSERT_FALSE(would_match_scope("/usr/include/coz.h", default_scope));
  ASSERT_FALSE(would_match_scope("/usr/include/coz.h", explicit_scope));
}

TEST(empty_scope_allows_everything) {
  HomeGuard hg("/home/user");
  std::unordered_set<std::string> empty_scope;
  ASSERT_TRUE(would_match_scope("/rustc/hash/file.rs", empty_scope));
  ASSERT_TRUE(would_match_scope("/home/user/.cargo/registry/src/lib.rs", empty_scope));
  ASSERT_TRUE(would_match_scope("/home/user/project/src/main.rs", empty_scope));
}

int main() {
  // Tests are run by static initializers above
  printf("%d/%d tests passed\n", tests_passed, tests_run);
  if(tests_passed != tests_run) {
    printf("SOME TESTS FAILED\n");
    return 1;
  }
  printf("ALL TESTS PASSED\n");
  return 0;
}
