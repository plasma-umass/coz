/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

#ifndef COZ_PATH_FILTER_H
#define COZ_PATH_FILTER_H

#include <cstdlib>
#include <string>
#include <vector>

inline bool path_has_prefix(const std::string& path, const std::string& prefix) {
  if(prefix.empty())
    return false;
  if(prefix.size() > path.size())
    return false;
  if(path.compare(0, prefix.size(), prefix) != 0)
    return false;
  return path.size() == prefix.size() || path[prefix.size()] == '/';
}

inline bool is_system_path(const std::string& normalized) {
  static const std::vector<std::string> prefixes = {
    "/usr/include",
    "/usr/lib",
    "/usr/local/include",
    "/usr/local/lib",
    "/lib",
    "/lib64"
  };
  for(const auto& prefix : prefixes) {
    if(path_has_prefix(normalized, prefix))
      return true;
  }
  return false;
}

inline bool is_rust_path(const std::string& normalized) {
  // /rustc/<hash>/... paths are synthetic, always start with /rustc
  if(path_has_prefix(normalized, "/rustc"))
    return true;
  // .rustup and .cargo are only Rust toolchain paths when under $HOME
  const char* home = std::getenv("HOME");
  if(home && home[0] != '\0') {
    std::string h(home);
    if(path_has_prefix(normalized, h + "/.rustup"))
      return true;
    if(path_has_prefix(normalized, h + "/.cargo/registry"))
      return true;
    if(path_has_prefix(normalized, h + "/.cargo/git"))
      return true;
  }
  return false;
}

inline bool is_coz_header(const std::string& path) {
  const std::string suffix = "/coz.h";
  return path.size() >= suffix.size() &&
         path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0;
}

#endif  // COZ_PATH_FILTER_H
