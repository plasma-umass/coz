/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

#include "lief_loader.h"

#include <LIEF/LIEF.hpp>
#include <dwarf++.hh>

#include <cstring>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include <iostream>  // Debug

#ifdef __APPLE__
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

// Set to 1 to enable debug output
#define LIEF_LOADER_DEBUG 0

namespace lief_loader {
namespace {

// Storage for section data - keeps the binary alive and provides raw pointers
struct section_view {
  const uint8_t* data;
  size_t size;
};

/**
 * LIEF-based implementation of dwarf::loader.
 * Extracts DWARF sections from ELF or Mach-O binaries.
 */
class lief_dwarf_loader : public dwarf::loader {
public:
  lief_dwarf_loader(std::unique_ptr<LIEF::Binary> binary,
                    std::map<dwarf::section_type, section_view> sections)
      : _binary(std::move(binary)),
        _sections(std::move(sections)) {}

  const void* load(dwarf::section_type section, size_t* size_out) override {
    auto it = _sections.find(section);
    if (it == _sections.end())
      return nullptr;
    if (size_out)
      *size_out = it->second.size;
    return it->second.data;
  }

private:
  std::unique_ptr<LIEF::Binary> _binary;  // Owns the memory
  std::map<dwarf::section_type, section_view> _sections;
};

/**
 * Map a section name to a DWARF section type.
 * Handles both ELF (.debug_*) and Mach-O (__debug_*) naming conventions.
 * Supports DWARF 2-5 section types as provided by plasma-umass/libelfin.
 */
bool section_name_to_type(const std::string& name, dwarf::section_type* out) {
  // ELF uses .debug_*, Mach-O uses __debug_*
  const char* suffix = nullptr;

  if (name.compare(0, 7, ".debug_") == 0) {
    suffix = name.c_str() + 7;
  } else if (name.compare(0, 8, "__debug_") == 0) {
    suffix = name.c_str() + 8;
  } else {
    return false;
  }

  // Map suffix to section_type (includes DWARF 5 sections)
  if (std::strcmp(suffix, "abbrev") == 0) {
    *out = dwarf::section_type::abbrev;
  } else if (std::strcmp(suffix, "addr") == 0) {
    *out = dwarf::section_type::addr;
  } else if (std::strcmp(suffix, "aranges") == 0) {
    *out = dwarf::section_type::aranges;
  } else if (std::strcmp(suffix, "frame") == 0) {
    *out = dwarf::section_type::frame;
  } else if (std::strcmp(suffix, "info") == 0) {
    *out = dwarf::section_type::info;
  } else if (std::strcmp(suffix, "line") == 0) {
    *out = dwarf::section_type::line;
  } else if (std::strcmp(suffix, "line_str") == 0) {
    *out = dwarf::section_type::line_str;
  } else if (std::strcmp(suffix, "loc") == 0) {
    *out = dwarf::section_type::loc;
  } else if (std::strcmp(suffix, "macinfo") == 0) {
    *out = dwarf::section_type::macinfo;
  } else if (std::strcmp(suffix, "pubnames") == 0) {
    *out = dwarf::section_type::pubnames;
  } else if (std::strcmp(suffix, "pubtypes") == 0) {
    *out = dwarf::section_type::pubtypes;
  } else if (std::strcmp(suffix, "ranges") == 0) {
    *out = dwarf::section_type::ranges;
  } else if (std::strcmp(suffix, "rnglists") == 0) {
    *out = dwarf::section_type::rnglists;
  } else if (std::strcmp(suffix, "str") == 0) {
    *out = dwarf::section_type::str;
  // DWARF 5 str_offsets - handle both full name and Mach-O truncated name (16-char limit)
  } else if (std::strcmp(suffix, "str_offsets") == 0 || std::strcmp(suffix, "str_offs") == 0) {
    *out = dwarf::section_type::str_offsets;
  } else if (std::strcmp(suffix, "types") == 0) {
    *out = dwarf::section_type::types;
  } else {
    return false;
  }

  return true;
}

/**
 * Extract DWARF sections from a LIEF ELF binary.
 */
std::map<dwarf::section_type, section_view> extract_elf_sections(LIEF::ELF::Binary* elf) {
  std::map<dwarf::section_type, section_view> result;

  for (const auto& section : elf->sections()) {
    dwarf::section_type type;
    if (section_name_to_type(section.name(), &type)) {
      auto content = section.content();
      if (!content.empty()) {
        result[type] = section_view{content.data(), content.size()};
      }
    }
  }

  return result;
}

#ifdef __APPLE__
/**
 * Extract DWARF sections from a LIEF Mach-O binary.
 */
std::map<dwarf::section_type, section_view> extract_macho_sections(LIEF::MachO::Binary* macho) {
  std::map<dwarf::section_type, section_view> result;

#if LIEF_LOADER_DEBUG
  std::cerr << "[lief_loader] Extracting Mach-O sections from binary" << std::endl;
#endif

  for (const auto& section : macho->sections()) {
    // Mach-O DWARF sections are in the __DWARF segment
    if (section.segment_name() != "__DWARF")
      continue;

#if LIEF_LOADER_DEBUG
    std::cerr << "[lief_loader] Found DWARF section: " << section.name()
              << " size=" << section.size() << std::endl;
#endif

    dwarf::section_type type;
    if (section_name_to_type(section.name(), &type)) {
      auto content = section.content();
      if (!content.empty()) {
        result[type] = section_view{content.data(), content.size()};
#if LIEF_LOADER_DEBUG
        std::cerr << "[lief_loader]   -> Mapped to type, content size=" << content.size() << std::endl;
#endif
      }
    }
  }

#if LIEF_LOADER_DEBUG
  std::cerr << "[lief_loader] Extracted " << result.size() << " DWARF sections" << std::endl;
#endif

  return result;
}
#endif

/**
 * Extract DWARF sections from any LIEF binary.
 */
std::map<dwarf::section_type, section_view> extract_sections(LIEF::Binary* binary) {
  if (auto* elf = dynamic_cast<LIEF::ELF::Binary*>(binary)) {
    return extract_elf_sections(elf);
  }
#ifdef __APPLE__
  if (auto* macho = dynamic_cast<LIEF::MachO::Binary*>(binary)) {
    return extract_macho_sections(macho);
  }
#endif
  return {};
}

/**
 * Check if sections contain DWARF debug info.
 */
bool has_debug_info(const std::map<dwarf::section_type, section_view>& sections) {
  return sections.find(dwarf::section_type::info) != sections.end();
}

/**
 * Check if a file exists.
 */
bool file_exists(const std::string& path) {
  struct stat st{};
  return stat(path.c_str(), &st) == 0;
}

/**
 * Extract directory from path.
 */
std::string dirname_of(const std::string& path) {
  auto pos = path.find_last_of('/');
  if (pos == std::string::npos)
    return ".";
  if (pos == 0)
    return "/";
  return path.substr(0, pos);
}

/**
 * Extract filename from path.
 */
std::string basename_of(const std::string& path) {
  auto pos = path.find_last_of('/');
  if (pos == std::string::npos)
    return path;
  return path.substr(pos + 1);
}

#ifndef __APPLE__
// ============== Linux-specific debug file lookup ==============

/**
 * Extract build-id from an ELF binary using LIEF.
 * Returns hex string of build-id, or empty string if not found.
 */
std::string extract_build_id(LIEF::ELF::Binary* elf) {
  // Look for GNU build-id note
  for (const auto& note : elf->notes()) {
    if (note.type() == LIEF::ELF::Note::TYPE::GNU_BUILD_ID) {
      auto desc = note.description();
      if (desc.empty())
        continue;

      // Convert to hex string
      std::ostringstream ss;
      for (uint8_t byte : desc) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
      }
      return ss.str();
    }
  }
  return std::string();
}

/**
 * Extract .gnu_debuglink filename from an ELF binary.
 * Returns the debug file name, or empty string if not found.
 */
std::string extract_debuglink(LIEF::ELF::Binary* elf) {
  const auto* section = elf->get_section(".gnu_debuglink");
  if (!section)
    return std::string();

  auto content = section->content();
  if (content.empty())
    return std::string();

  // The section contains a null-terminated filename followed by padding and CRC
  return std::string(reinterpret_cast<const char*>(content.data()));
}

/**
 * Find separate debug file for an ELF binary.
 * Searches using build-id and .gnu_debuglink.
 */
std::string find_debug_file(LIEF::ELF::Binary* elf, const std::string& binary_path) {
  std::vector<std::string> search_paths;
  std::string directory = dirname_of(binary_path);

  // 1. Try build-id based lookup first (most reliable)
  std::string build_id = extract_build_id(elf);
  if (build_id.length() >= 3) {
    std::string prefix = build_id.substr(0, 2);
    std::string suffix = build_id.substr(2);
    search_paths.push_back("/usr/lib/debug/.build-id/" + prefix + "/" + suffix + ".debug");
  }

  // 2. Try .gnu_debuglink based lookup
  std::string debuglink = extract_debuglink(elf);
  if (!debuglink.empty()) {
    // Standard search order per GDB documentation
    search_paths.push_back(directory + "/" + debuglink);
    search_paths.push_back(directory + "/.debug/" + debuglink);
    search_paths.push_back("/usr/lib/debug" + directory + "/" + debuglink);
  }

  // Search all paths
  for (const auto& path : search_paths) {
    if (file_exists(path)) {
      return path;
    }
  }

  return std::string();
}

#else
// ============== macOS-specific debug file lookup ==============

/**
 * Create a temporary directory for generated dSYM bundles.
 */
std::string make_temp_dir() {
  std::string tmpl = "/tmp/coz-dsym-XXXXXX";
  std::vector<char> buffer(tmpl.begin(), tmpl.end());
  buffer.push_back('\0');
  char* created = mkdtemp(buffer.data());
  if (!created)
    return std::string();
  return std::string(created);
}

/**
 * Run dsymutil to generate debug symbols.
 */
bool run_dsymutil(const std::string& binary_path, const std::string& bundle_path) {
  std::vector<const char*> args = {
    "dsymutil",
    binary_path.c_str(),
    "-o",
    bundle_path.c_str(),
    nullptr
  };

  // Filter out DYLD_* environment variables that could interfere with dsymutil
  std::vector<std::string> env_storage;
  std::vector<char*> envp;
  if (environ) {
    for (char** entry = environ; *entry != nullptr; ++entry) {
      if (std::strncmp(*entry, "DYLD_INSERT_LIBRARIES=", 22) == 0)
        continue;
      if (std::strncmp(*entry, "DYLD_FORCE_FLAT_NAMESPACE=", 26) == 0)
        continue;
      env_storage.emplace_back(*entry);
    }
  }
  for (auto& entry : env_storage) {
    envp.push_back(const_cast<char*>(entry.c_str()));
  }
  envp.push_back(nullptr);

  pid_t pid = 0;
  int rc = posix_spawnp(&pid, "dsymutil", nullptr, nullptr,
                        const_cast<char* const*>(args.data()),
                        envp.data());
  if (rc != 0) {
    return false;
  }

  int status = 0;
  if (waitpid(pid, &status, 0) == -1) {
    return false;
  }

  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

#endif // __APPLE__

} // anonymous namespace

std::string find_dsym_path(const std::string& binary_path) {
#ifdef __APPLE__
  std::string name = basename_of(binary_path);
  if (name.empty())
    return std::string();

  std::string bundle = binary_path + ".dSYM/Contents/Resources/DWARF/" + name;
  if (file_exists(bundle))
    return bundle;
#endif
  return std::string();
}

std::string generate_dsym(const std::string& binary_path) {
#ifdef __APPLE__
  static std::mutex mutex;
  static std::unordered_map<std::string, std::string> generated;

  // Check cache first
  {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = generated.find(binary_path);
    if (it != generated.end())
      return it->second;
  }

  std::string temp_dir = make_temp_dir();
  if (temp_dir.empty())
    return std::string();

  std::string name = basename_of(binary_path);
  std::string bundle = temp_dir + "/" + name + ".dSYM";

  if (!run_dsymutil(binary_path, bundle))
    return std::string();

  std::string dwarf_path = bundle + "/Contents/Resources/DWARF/" + name;

  // Cache the result
  {
    std::lock_guard<std::mutex> lock(mutex);
    generated.emplace(binary_path, dwarf_path);
  }

  return dwarf_path;
#else
  (void)binary_path;
  return std::string();
#endif
}

bool is_static_executable(const std::string& path) {
#ifdef __APPLE__
  // macOS uses ASLR slide, always return false
  (void)path;
  return false;
#else
  auto binary = LIEF::Parser::parse(path);
  if (!binary)
    return false;

  auto* elf = dynamic_cast<LIEF::ELF::Binary*>(binary.get());
  if (!elf)
    return false;

  // ET_EXEC is a static executable (load at fixed address, typically 0)
  // ET_DYN is a shared library or PIE executable (use provided load address)
  return elf->header().file_type() == LIEF::ELF::Header::FILE_TYPE::EXEC;
#endif
}

std::shared_ptr<dwarf::loader> load(const std::string& path) {
#if LIEF_LOADER_DEBUG
  std::cerr << "[lief_loader] Loading: " << path << std::endl;
  std::cerr.flush();
#endif

  // Try loading the binary directly first
  std::unique_ptr<LIEF::Binary> binary;
#if LIEF_LOADER_DEBUG
  std::cerr << "[lief_loader] About to parse binary" << std::endl;
  std::cerr.flush();
#endif
  try {
#ifdef __APPLE__
    // On macOS, use Mach-O specific parser
    auto fat_binary = LIEF::MachO::Parser::parse(path);
    if (fat_binary && !fat_binary->empty()) {
      // Take the first (or only) binary from the fat archive
      binary = fat_binary->take(0);
    }
#else
    binary = LIEF::Parser::parse(path);
#endif
#if LIEF_LOADER_DEBUG
    std::cerr << "[lief_loader] Parse returned" << std::endl;
    std::cerr.flush();
#endif
  } catch (const std::exception& e) {
#if LIEF_LOADER_DEBUG
    std::cerr << "[lief_loader] Exception parsing binary: " << e.what() << std::endl;
#endif
    return nullptr;
  }

  if (!binary) {
#if LIEF_LOADER_DEBUG
    std::cerr << "[lief_loader] Failed to parse binary" << std::endl;
#endif
    return nullptr;
  }

#if LIEF_LOADER_DEBUG
  std::cerr << "[lief_loader] Binary parsed successfully, format="
            << static_cast<int>(binary->format()) << std::endl;
#endif

  std::map<dwarf::section_type, section_view> sections;
  try {
    sections = extract_sections(binary.get());
  } catch (const std::exception& e) {
#if LIEF_LOADER_DEBUG
    std::cerr << "[lief_loader] Exception extracting sections: " << e.what() << std::endl;
#endif
    return nullptr;
  }

#if LIEF_LOADER_DEBUG
  std::cerr << "[lief_loader] Extracted " << sections.size() << " sections" << std::endl;
  for (const auto& [type, view] : sections) {
    std::cerr << "[lief_loader]   section type " << static_cast<int>(type)
              << ": size=" << view.size << " data=" << (void*)view.data << std::endl;
  }
#endif

  // If we have debug info embedded, use it directly
  if (has_debug_info(sections)) {
#if LIEF_LOADER_DEBUG
    std::cerr << "[lief_loader] Found embedded debug info" << std::endl;
#endif
    return std::make_shared<lief_dwarf_loader>(std::move(binary), std::move(sections));
  }

#ifdef __APPLE__
  // === macOS: Try dSYM bundle ===
#if LIEF_LOADER_DEBUG
  std::cerr << "[lief_loader] No embedded debug info, trying dSYM" << std::endl;
#endif

  // Look for existing .dSYM bundle
  std::string dsym_path = find_dsym_path(path);
#if LIEF_LOADER_DEBUG
  std::cerr << "[lief_loader] find_dsym_path returned: " << dsym_path << std::endl;
#endif

  // Generate dSYM if needed
  if (dsym_path.empty()) {
    dsym_path = generate_dsym(path);
#if LIEF_LOADER_DEBUG
    std::cerr << "[lief_loader] generate_dsym returned: " << dsym_path << std::endl;
#endif
  }

  if (!dsym_path.empty()) {
    // LIEF::MachO::Parser handles fat binaries automatically
    auto dsym_binary = LIEF::Parser::parse(dsym_path);
    if (dsym_binary) {
#if LIEF_LOADER_DEBUG
      std::cerr << "[lief_loader] dSYM parsed successfully" << std::endl;
#endif
      auto dsym_sections = extract_sections(dsym_binary.get());
      if (has_debug_info(dsym_sections)) {
#if LIEF_LOADER_DEBUG
        std::cerr << "[lief_loader] Found debug info in dSYM" << std::endl;
#endif
        return std::make_shared<lief_dwarf_loader>(std::move(dsym_binary), std::move(dsym_sections));
      }
    }
  }

#else
  // === Linux: Try separate debug files ===

  auto* elf = dynamic_cast<LIEF::ELF::Binary*>(binary.get());
  if (elf) {
    std::string debug_path = find_debug_file(elf, path);
    if (!debug_path.empty()) {
      auto debug_binary = LIEF::Parser::parse(debug_path);
      if (debug_binary) {
        auto debug_sections = extract_sections(debug_binary.get());
        if (has_debug_info(debug_sections)) {
          return std::make_shared<lief_dwarf_loader>(std::move(debug_binary), std::move(debug_sections));
        }
      }
    }
  }
#endif

  // No debug info found
  return nullptr;
}

} // namespace lief_loader
