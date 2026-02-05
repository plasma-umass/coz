/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

#ifndef COZ_MACHO_SUPPORT_H
#define COZ_MACHO_SUPPORT_H

#ifdef __APPLE__

#include <memory>
#include <string>

namespace dwarf {
class loader;
}

namespace macho_support {

/**
 * Find an existing .dSYM bundle for the given binary path.
 * Returns the path to the DWARF file inside the bundle, or empty string if not found.
 */
std::string find_dsym_bundle(const std::string& binary_path);

/**
 * Extract the basename from a path.
 */
std::string basename_for(const std::string& path);

/**
 * Create a temporary directory for generated dSYM bundles.
 * Returns the path to the created directory, or empty string on failure.
 */
std::string make_temp_dir();

/**
 * Run dsymutil to generate debug symbols for a binary.
 * Returns true on success, false on failure.
 */
bool run_dsymutil(const std::string& binary_path, const std::string& bundle_path);

/**
 * Generate a dSYM bundle for the given binary if one doesn't exist.
 * Returns the path to the DWARF file inside the bundle, or empty string on failure.
 */
std::string generate_dsym_bundle(const std::string& binary_path);

/**
 * Load DWARF debug information from a Mach-O binary.
 * This will check for an existing .dSYM bundle, generate one if needed,
 * and return a dwarf::loader for the debug information.
 */
std::shared_ptr<dwarf::loader> load_debug_info(const std::string& binary_path);

} // namespace macho_support

#endif // __APPLE__

#endif // COZ_MACHO_SUPPORT_H
