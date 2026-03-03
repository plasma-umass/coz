/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

#ifndef COZ_LIEF_LOADER_H
#define COZ_LIEF_LOADER_H

#include <memory>
#include <string>

namespace dwarf {
class loader;
}

namespace lief_loader {

/**
 * Load DWARF debug information from a binary file using LIEF.
 * Works for both ELF (Linux) and Mach-O (macOS) binaries.
 *
 * On macOS, this will:
 *   1. Check for an existing .dSYM bundle
 *   2. Generate one via dsymutil if needed
 *   3. Load DWARF sections from the dSYM or binary
 *
 * On Linux, this will:
 *   1. Check for .debug_info in the binary
 *   2. Look for separate debug files via .gnu_debuglink or build-id
 *
 * Returns nullptr if debug information cannot be loaded.
 */
std::shared_ptr<dwarf::loader> load(const std::string& path);

/**
 * Find an existing .dSYM bundle for a macOS binary.
 * Returns the path to the DWARF file inside the bundle, or empty string if not found.
 */
std::string find_dsym_path(const std::string& binary_path);

/**
 * Generate a .dSYM bundle for a macOS binary using dsymutil.
 * Returns the path to the DWARF file inside the generated bundle, or empty string on failure.
 */
std::string generate_dsym(const std::string& binary_path);

/**
 * Check if an ELF binary is a static executable (ET_EXEC) vs shared/PIE (ET_DYN).
 * Returns true if the binary is ET_EXEC (should use load_address=0).
 * Returns false for ET_DYN (shared libraries and PIE executables).
 * On macOS, always returns false (Mach-O uses ASLR slide).
 */
bool is_static_executable(const std::string& path);

} // namespace lief_loader

#endif // COZ_LIEF_LOADER_H
