/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

#include "inspect.h"

#ifdef __APPLE__
  #include "elf_compat.h"
  #include <mach-o/dyld.h>
  #include "macho_support.h"
#else
  #include <elf.h>
#endif
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdint>
#include <iostream>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <algorithm>
#include <libelfin/dwarf/dwarf++.hh>
#include <libelfin/elf/elf++.hh>

#include "util.h"

#include "ccutil/log.h"

using namespace std;

static dwarf::value find_attribute(const dwarf::die& d, dwarf::DW_AT attr);

/**
 * Locate the build ID encoded in an ELF file and return it as a formatted string
 */
static string find_build_id(elf::elf& f) {
  for(auto& section : f.sections()) {
    if(section.get_hdr().type == elf::sht::note) {
      uintptr_t base = reinterpret_cast<uintptr_t>(section.data());
      size_t offset = 0;
      while(offset < section.size()) {
        Elf64_Nhdr* hdr = reinterpret_cast<Elf64_Nhdr*>(base + offset);

        if(hdr->n_type == NT_GNU_BUILD_ID) {
          // Found the build-id note
          stringstream ss;
          uintptr_t desc_base = base + offset + sizeof(Elf64_Nhdr) + hdr->n_namesz;
          uint8_t* build_id = reinterpret_cast<uint8_t*>(desc_base);
          for(size_t i = 0; i < hdr->n_descsz; i++) {
            ss.flags(ios::hex);
            ss.width(2);
            ss.fill('0');
            ss << static_cast<size_t>(build_id[i]);
          }
          return ss.str();

        } else {
          // Advance to the next note header
          offset += sizeof(Elf64_Nhdr) + hdr->n_namesz + hdr->n_descsz;
        }
      }
    }
  }
  return "";
}

static string absolute_path(const string filename) {
  if(filename[0] == '/') return filename;

  char* cwd = getcwd(NULL, 0);
  REQUIRE(cwd != NULL) << "Failed to get current directory";

  return string(cwd) + '/' + filename;
}

static string canonicalize_path(const string filename) {
  vector<string> parts = split(absolute_path(filename), '/');

  // Iterate over the path parts to produce a reduced list of path sections
  vector<string> reduced;
  for(string part : parts) {
    if(part == "..") {
      REQUIRE(reduced.size() > 0) << "Invalid absolute path";
      reduced.pop_back();
    } else if(part.length() > 0 && part != ".") {
      // Skip single-dot or empty entries
      reduced.push_back(part);
    }
  }

  // Join path sections into a single string
  string result;
  for(string part : reduced) {
    result += "/" + part;
  }

  return result;
}

static bool file_exists(const string& filename) {
  struct stat statbuf;
  int rc = stat(filename.c_str(), &statbuf);
  // If the stat call succeeds, the file must exist
  return rc == 0;
}

/**
 * Get the full path to a file specified via absolute path, relative path, or raw name
 * resolved via the PATH variable.
 */
static const string get_full_path(const string filename) {
  if(filename.find('/') != string::npos) {
    return canonicalize_path(filename);

  } else {
    // Search the environment's path for the first match
    const string path_env = getenv("PATH");
    vector<string> search_dirs = split(getenv_safe("PATH", ":"));

    for(const string& dir : search_dirs) {
      string full_path = dir + '/' + filename;
      if(file_exists(full_path)) {
        return full_path;
      }
    }
  }

  return "";
}

/**
 * Locate an ELF file that contains debug symbols for the file provided by name.
 * This will work for files specified by relative path, absolute path, or raw name
 * resolved via the PATH variable.
 */
#ifndef __APPLE__
static elf::elf locate_debug_executable(const string filename) {
  elf::elf f;

  const string full_path = get_full_path(filename);

  // If a full path wasn't found, return the invalid ELF file
  if(full_path.length() == 0) {
    WARNING << "Full path is empty, returning invalid elf";
    return f;
  }

  int fd = open(full_path.c_str(), O_RDONLY);

  // If the file couldn't be opened, return the invalid ELF file
  if(fd < 0) {
    return f;
  }

  // Load the opened ELF file
  f = elf::elf(elf::create_mmap_loader(fd));

  // If this file has a .debug_info section, return it
  if(f.get_section(".debug_info").valid()) {
    return f;
  }

#ifdef __APPLE__
  // On macOS, check for .dSYM bundle
  string binary_name = full_path.substr(full_path.find_last_of('/') + 1);
  string dsym_path = full_path + ".dSYM/Contents/Resources/DWARF/" + binary_name;

  int dsym_fd = open(dsym_path.c_str(), O_RDONLY);
  if(dsym_fd >= 0) {
    try {
      elf::elf dsym_f = elf::elf(elf::create_mmap_loader(dsym_fd));
      auto debug_info = dsym_f.get_section(".debug_info");
      if(debug_info.valid()) {
        return dsym_f;
      }
    } catch (const std::exception& e) {
      (void)e;
    }
  }
#endif

  // If there isn't a .debug_info section, check for the .gnu_debuglink section
  auto& link_section = f.get_section(".gnu_debuglink");

  // Store the full path to the executable and its directory name
  string directory = full_path.substr(0, full_path.find_last_of('/'));

  // Build a set of paths to search for a debug version of the file
  vector<string> search_paths;

  // Check for a build-id section
  string build_id = find_build_id(f);
  if(build_id.length() > 0) {
    string prefix = build_id.substr(0, 2);
    string suffix = build_id.substr(2);

    auto p = string("/usr/lib/debug/.build-id/") + prefix + "/" + suffix + ".debug";
    search_paths.push_back(p);
  }

  // Check for a debug_link section
  if(link_section.valid()) {
    string link_name = reinterpret_cast<const char*>(link_section.data());

    search_paths.push_back(directory + "/" + link_name);
    search_paths.push_back(directory + "/.debug/" + link_name);
    search_paths.push_back("/usr/lib/debug" + directory + "/" + link_name);
  }

  // Clear the loaded file so if we have to return it, it won't be valid()
  f = elf::elf();

  // Try all the usable search paths
  for(const string& path : search_paths) {
    fd = open(path.c_str(), O_RDONLY);
    if(fd >= 0) {
      f = elf::elf(elf::create_mmap_loader(fd));
      if(f.get_section(".debug_info").valid()) {
        break;
      }
      f = elf::elf();
    }
  }

  return f;
}
#endif // !__APPLE__

#ifdef __APPLE__
unordered_map<string, uintptr_t> get_loaded_files() {
  unordered_map<string, uintptr_t> result;

  uint32_t image_count = _dyld_image_count();
  for(uint32_t i = 0; i < image_count; i++) {
    const mach_header* header = _dyld_get_image_header(i);
    const char* image_name = _dyld_get_image_name(i);
    if(header == nullptr || image_name == nullptr)
      continue;
    string path = canonicalize_path(image_name);
    if(path.empty())
      continue;
    intptr_t slide = _dyld_get_image_vmaddr_slide(i);
    result.emplace(path, static_cast<uintptr_t>(slide));
  }

  return result;
}
#else
unordered_map<string, uintptr_t> get_loaded_files() {
  unordered_map<string, uintptr_t> result;

  ifstream maps("/proc/self/maps");
  while(maps.good() && !maps.eof()) {
    uintptr_t base, limit;
    char perms[5];
    size_t offset;
    size_t dev_major, dev_minor;
    uintptr_t inode;
    string path;

    // Skip over whitespace
    maps >> skipws;

    // Read in "<base>-<limit> <perms> <offset> <dev_major>:<dev_minor> <inode>"
    maps >> std::hex >> base;
    if(maps.get() != '-') break;
    maps >> std::hex >> limit;

    if(maps.get() != ' ') break;
    maps.get(perms, 5);

    maps >> std::hex >> offset;
    maps >> std::hex >> dev_major;
    if(maps.get() != ':') break;
    maps >> std::hex >> dev_minor;
    maps >> std::dec >> inode;

    // Skip over spaces and tabs
    while(maps.peek() == ' ' || maps.peek() == '\t') { maps.ignore(1); }

    // Read out the mapped file's path
    getline(maps, path);

    // If this is an executable mapping of an absolute path, include it
    if(perms[2] == 'x' && path[0] == '/') {
      result[path] = base - offset;
    }
  }

  return result;
}
#endif

bool wildcard_match(string::const_iterator subject,
                    string::const_iterator subject_end,
                    string::const_iterator pattern,
                    string::const_iterator pattern_end) {

  if((pattern == pattern_end) != (subject == subject_end)) {
    // If one but not both of the iterators have finished, match failed
    return false;
  } else if(pattern == pattern_end && subject == subject_end) {
    // If both iterators have finished, match succeeded
    return true;

  } else if(*pattern == '%') {
    // Try possible matches of the wildcard, starting with the longest possible match
    for(auto match_end = subject_end; match_end >= subject; match_end--) {
      if(wildcard_match(match_end, subject_end, pattern+1, pattern_end)) {
        return true;
      }
    }
    // No matches found. Abort
    return false;

  } else {
    // Walk through non-wildcard characters to match
    while(subject != subject_end && pattern != pattern_end && *pattern != '%') {
      // If the characters do not match, abort. Otherwise keep going.
      if(*pattern != *subject) {
        return false;
      } else {
        pattern++;
        subject++;
      }
    }

    // Recursive call to handle wildcard or termination cases
    return wildcard_match(subject, subject_end, pattern, pattern_end);
  }
}

bool wildcard_match(const string& subject, const string& pattern) {
  return wildcard_match(subject.begin(), subject.end(), pattern.begin(), pattern.end());
}

static bool in_scope_normalized(const string& normalized,
                                const unordered_set<string>& scope) {
  for(const string& pattern : scope) {
    if(wildcard_match(normalized, pattern)) {
      return true;
    }
  }
  return false;
}

bool in_scope(const string& name, const unordered_set<string>& scope) {
  string normalized = canonicalize_path(name);
  return in_scope_normalized(normalized, scope);
}

static bool path_has_prefix(const string& path, const string& prefix) {
  if(prefix.empty())
    return false;
  if(prefix.size() > path.size())
    return false;
  if(path.compare(0, prefix.size(), prefix) != 0)
    return false;
  return path.size() == prefix.size() || path[prefix.size()] == '/';
}

static bool is_system_path(const string& normalized) {
  static const vector<string> prefixes = {
    "/usr/include",
    "/usr/lib",
    "/usr/local/include",
    "/usr/local/lib",
    "/lib",
    "/lib64",
    // macOS system paths
    "/Applications/Xcode.app",
    "/Library/Developer",
    "/System/Library"
  };
  for(const auto& prefix : prefixes) {
    if(path_has_prefix(normalized, prefix))
      return true;
  }
  return false;
}

static bool file_matches_scope(const string& name,
                               const unordered_set<string>& scope,
                               bool allow_system_sources) {
  if(name.empty())
    return false;
  string normalized = canonicalize_path(name);
  if(!allow_system_sources && is_system_path(normalized))
    return false;
  if(scope.empty())
    return true;
  return in_scope_normalized(normalized, scope);
}

static void enqueue_range(vector<memory_map::queued_range>& pending,
                          const string& filename,
                          size_t line_no,
                          interval range,
                          bool preferred = false) {
  if(filename.empty())
    return;
  pending.push_back(memory_map::queued_range{filename, line_no, range, preferred});
}

struct subprogram_range {
  uintptr_t low;
  uintptr_t high;
  std::string filename;
  size_t line;
  bool in_scope;
};

static void collect_subprogram_ranges(const dwarf::die& d,
                                      const dwarf::line_table& table,
                                      const unordered_set<string>& source_scope,
                                      bool allow_system_sources,
                                      vector<subprogram_range>& ranges) {
  if(!d.valid())
    return;

  try {
    if(d.tag == dwarf::DW_TAG::subprogram) {
      string decl_file;
      dwarf::value decl_file_val = find_attribute(d, dwarf::DW_AT::decl_file);
      if(decl_file_val.valid() &&
         decl_file_val.get_type() == dwarf::value::type::uconstant &&
         table.valid()) {
        decl_file = table.get_file(decl_file_val.as_uconstant())->path;
        decl_file = canonicalize_path(decl_file);
      }

      size_t decl_line = 0;
      dwarf::value decl_line_val = find_attribute(d, dwarf::DW_AT::decl_line);
      if(decl_line_val.valid()) {
        if(decl_line_val.get_type() == dwarf::value::type::uconstant)
          decl_line = decl_line_val.as_uconstant();
        else if(decl_line_val.get_type() == dwarf::value::type::sconstant)
          decl_line = decl_line_val.as_sconstant();
      }

      bool file_in_scope = decl_file.size() > 0 &&
                           file_matches_scope(decl_file, source_scope, allow_system_sources);

      if(file_in_scope && decl_line > 0) {
        dwarf::value ranges_val = find_attribute(d, dwarf::DW_AT::ranges);
        if(ranges_val.valid()) {
          for(auto r : ranges_val.as_rangelist()) {
            ranges.push_back(subprogram_range{r.low, r.high, decl_file, decl_line, true});
          }
        } else {
          dwarf::value low_pc_val = find_attribute(d, dwarf::DW_AT::low_pc);
          dwarf::value high_pc_val = find_attribute(d, dwarf::DW_AT::high_pc);
          if(low_pc_val.valid() && high_pc_val.valid()) {
            uintptr_t low_pc = 0;
            uintptr_t high_pc = 0;

            if(low_pc_val.get_type() == dwarf::value::type::address)
              low_pc = low_pc_val.as_address();
            else if(low_pc_val.get_type() == dwarf::value::type::uconstant)
              low_pc = low_pc_val.as_uconstant();
            else if(low_pc_val.get_type() == dwarf::value::type::sconstant)
              low_pc = low_pc_val.as_sconstant();

            if(high_pc_val.get_type() == dwarf::value::type::address)
              high_pc = high_pc_val.as_address();
            else if(high_pc_val.get_type() == dwarf::value::type::uconstant)
              high_pc = high_pc_val.as_uconstant();
            else if(high_pc_val.get_type() == dwarf::value::type::sconstant)
              high_pc = high_pc_val.as_sconstant();

            if(high_pc > low_pc) {
              ranges.push_back(subprogram_range{low_pc, high_pc, decl_file, decl_line, true});
            }
          }
        }
      }
    }
  } catch(dwarf::format_error e) {
    (void)e;
  }

  for(const auto& child : d) {
    collect_subprogram_ranges(child, table, source_scope, allow_system_sources, ranges);
  }
}

static const subprogram_range* find_subprogram(const vector<subprogram_range>& ranges,
                                               uintptr_t addr) {
  if(ranges.empty())
    return nullptr;

  auto it = upper_bound(ranges.begin(),
                        ranges.end(),
                        addr,
                        [](uintptr_t value, const subprogram_range& range) {
                          return value < range.low;
                        });
  if(it == ranges.begin())
    return nullptr;

  --it;
  if(addr >= it->low && addr < it->high)
    return &(*it);
  return nullptr;
}

void memory_map::build(const unordered_set<string>& binary_scope,
                       const unordered_set<string>& source_scope,
                       bool allow_system_sources) {
  auto loaded = get_loaded_files();

  size_t in_scope_count = 0;
  for(const auto& f : loaded) {
    bool scoped = in_scope(f.first, binary_scope);
    if(scoped) {
      try {
        if(process_file(f.first, f.second, source_scope, allow_system_sources)) {
          INFO << "Including lines from executable " << f.first;
          in_scope_count++;
        } else {
          INFO << "Unable to locate debug information for " << f.first;
        }
      } catch(const system_error& e) {
        WARNING << "Processing file \"" << f.first << "\" failed: " << e.what();
      }
    }
  }

  REQUIRE(in_scope_count > 0)
    << "Debug information was not found for any in-scope executables or libraries";
}

dwarf::value find_attribute(const dwarf::die& d, dwarf::DW_AT attr) {
  if(!d.valid())
    return dwarf::value();

  try {
    if(d.has(attr))
      return d[attr];

    if(d.has(dwarf::DW_AT::abstract_origin)) {
      const dwarf::die child = d.resolve(dwarf::DW_AT::abstract_origin).as_reference();
      dwarf::value v = find_attribute(child, attr);
      if(v.valid())
        return v;
    }

    if(d.has(dwarf::DW_AT::specification)) {
      const dwarf::die child = d.resolve(dwarf::DW_AT::specification).as_reference();
      dwarf::value v = find_attribute(child, attr);
      if(v.valid())
        return v;
    }
  } catch(dwarf::format_error e) {
    (void)e;
  }

  return dwarf::value();
}

void memory_map::add_range(std::string filename, size_t line_no, interval range) {
  shared_ptr<file> f = get_file(filename);
  shared_ptr<line> l = f->get_line(line_no);
  _ranges.emplace(range, l);
}

void memory_map::process_inlines(const dwarf::die& d,
                                 const dwarf::line_table& table,
                                 const unordered_set<string>& source_scope,
                                 uintptr_t load_address,
                                 bool allow_system_sources,
                                 vector<memory_map::queued_range>& pending,
                                 const string& parent_file,
                                 size_t parent_line,
                                 bool parent_in_scope) {
  if(!d.valid())
    return;

  string attribution_file = parent_file;
  size_t attribution_line = parent_line;
  bool attribution_valid = parent_in_scope && !parent_file.empty();

  try {
    if(d.tag == dwarf::DW_TAG::inlined_subroutine) {
      string call_file;
      if(d.has(dwarf::DW_AT::call_file) && table.valid()) {
        call_file = table.get_file(d[dwarf::DW_AT::call_file].as_uconstant())->path;
        call_file = canonicalize_path(call_file);
      }

      size_t call_line = 0;
      if(d.has(dwarf::DW_AT::call_line)) {
        call_line = d[dwarf::DW_AT::call_line].as_uconstant();
      }

      bool call_in_scope = file_matches_scope(call_file, source_scope, allow_system_sources);
      bool prefer_new_attribution = !attribution_valid || !is_system_path(call_file);
      if(call_in_scope && !call_file.empty() && prefer_new_attribution) {
        attribution_file = call_file;
        attribution_line = call_line;
        attribution_valid = true;
      }

      if(attribution_valid) {
        dwarf::value ranges_val = find_attribute(d, dwarf::DW_AT::ranges);
        if(ranges_val.valid()) {
          for(auto r : ranges_val.as_rangelist()) {
            enqueue_range(pending,
                          attribution_file,
                          attribution_line,
                          interval(r.low, r.high) + load_address,
                          /*preferred=*/true);
          }
        } else {
          dwarf::value low_pc_val = find_attribute(d, dwarf::DW_AT::low_pc);
          dwarf::value high_pc_val = find_attribute(d, dwarf::DW_AT::high_pc);

          if(low_pc_val.valid() && high_pc_val.valid()) {
            uint64_t low_pc = 0;
            uint64_t high_pc = 0;

            if(low_pc_val.get_type() == dwarf::value::type::address)
              low_pc = low_pc_val.as_address();
            else if(low_pc_val.get_type() == dwarf::value::type::uconstant)
              low_pc = low_pc_val.as_uconstant();
            else if(low_pc_val.get_type() == dwarf::value::type::sconstant)
              low_pc = low_pc_val.as_sconstant();

            if(high_pc_val.get_type() == dwarf::value::type::address)
              high_pc = high_pc_val.as_address();
            else if(high_pc_val.get_type() == dwarf::value::type::uconstant)
              high_pc = high_pc_val.as_uconstant();
            else if(high_pc_val.get_type() == dwarf::value::type::sconstant)
              high_pc = high_pc_val.as_sconstant();

            if(high_pc > low_pc) {
              enqueue_range(pending,
                            attribution_file,
                            attribution_line,
                            interval(low_pc, high_pc) + load_address,
                            /*preferred=*/true);
            }
          }
        }
      }

      for(const auto& child : d) {
        process_inlines(child,
                        table,
                        source_scope,
                        load_address,
                        allow_system_sources,
                        pending,
                        attribution_file,
                        attribution_line,
                        attribution_valid);
      }
      return;
    }
  } catch(dwarf::format_error e) {
    (void)e;
  }

  for(const auto& child : d) {
    process_inlines(child,
                    table,
                    source_scope,
                    load_address,
                    allow_system_sources,
                    pending,
                    attribution_file,
                    attribution_line,
                    attribution_valid);
  }
}

bool memory_map::process_file(const string& name, uintptr_t load_address,
                              const unordered_set<string>& source_scope,
                              bool allow_system_sources) {
#ifdef __APPLE__
  std::shared_ptr<dwarf::loader> loader = macho_support::load_debug_info(name);
  if(!loader) {
    return false;
  }
  std::unique_ptr<dwarf::dwarf> dwarf_ptr;
  try {
    dwarf_ptr.reset(new dwarf::dwarf(loader));
  } catch(const std::exception& e) {
    WARNING << "Failed to load DWARF for " << name << ": " << e.what();
    return false;
  }
#else
  elf::elf f = locate_debug_executable(name);
  // If a debug version of the file could not be located, return false
  if(!f.valid()) {
    return false;
  }

  switch(f.get_hdr().type) {
    case elf::et::exec:
      // Loaded at base zero
      load_address = 0;
      break;

    case elf::et::dyn:
      // Load address should stay as-is
      break;

    default:
      WARNING << "Unsupported ELF file type...";
  }

  std::unique_ptr<dwarf::dwarf> dwarf_ptr;
  try {
    dwarf_ptr.reset(new dwarf::dwarf(dwarf::elf::create_loader(f)));
  } catch(const std::exception& e) {
    WARNING << "Failed to load DWARF for " << name << ": " << e.what();
    return false;
  }
#endif

  dwarf::dwarf& d = *dwarf_ptr;

  vector<memory_map::queued_range> pending;

  size_t cu_count = 0;
  // Walk through the compilation units (source files) in the executable
  for(auto unit : d.compilation_units()) {
    cu_count++;
    try {
      string prev_filename;
      size_t prev_line;
      uintptr_t prev_address = 0;
      set<string> included_files;
      const auto& table = unit.get_line_table();
      if(!table.valid())
        continue;
      vector<subprogram_range> subprograms;
      collect_subprogram_ranges(unit.root(),
                                table,
                                source_scope,
                                allow_system_sources,
                                subprograms);
      sort(subprograms.begin(), subprograms.end(),
           [](const subprogram_range& a, const subprogram_range& b) {
             if(a.low != b.low)
               return a.low < b.low;
             return a.high < b.high;
           });

      // Walk through the line instructions in the DWARF line table
      for(auto& line_info : table) {
        // Insert an entry if this isn't the first line command in the sequence
        bool scope_match = file_matches_scope(prev_filename, source_scope, allow_system_sources);
        if(scope_match) {
          if(prev_address != 0) {
            const subprogram_range* owner = find_subprogram(subprograms, prev_address);
            if(owner && owner->in_scope) {
              bool owner_is_system = is_system_path(owner->filename);
              bool prev_is_system = is_system_path(prev_filename);
              if(prev_is_system && !owner_is_system) {
                prev_filename = owner->filename;
                prev_line = owner->line;
              }
            }
          }
          if(prev_address != 0) {
            included_files.insert(prev_filename);
            enqueue_range(pending,
                          prev_filename,
                          prev_line,
                          interval(prev_address, line_info.address) + load_address);
          }
        }

        if(line_info.end_sequence) {
          prev_address = 0;
        } else {
          prev_filename = canonicalize_path(line_info.file->path);
          prev_line = line_info.line;
          prev_address = line_info.address;
        }
      }
      process_inlines(unit.root(),
                      table,
                      source_scope,
                      load_address,
                      allow_system_sources,
                      pending);

      for(const string& filename : included_files) {
        INFO << "Included source file " << filename;
      }

    } catch(dwarf::format_error& e) {
      // Skip compilation units with DWARF format errors (e.g., unsupported DWARF 5 features)
      (void)e;
    } catch(std::exception& e) {
      WARNING << "Exception processing DWARF: " << e.what();
    }
  }

  std::sort(pending.begin(), pending.end(),
            [](const memory_map::queued_range& a, const memory_map::queued_range& b) {
              if(a.range.get_base() != b.range.get_base())
                return a.range.get_base() < b.range.get_base();
              if(a.range.get_limit() != b.range.get_limit())
                return a.range.get_limit() < b.range.get_limit();
              if(a.preferred != b.preferred)
                return a.preferred && !b.preferred;
              if(a.line != b.line)
                return a.line < b.line;
              return a.filename < b.filename;
            });
  for(auto& entry : pending) {
    add_range(entry.filename, entry.line, entry.range);
  }

  return true;
}

shared_ptr<line> memory_map::find_line(const string& name) {
  string::size_type colon_pos = name.find_first_of(':');
  if(colon_pos == string::npos) {
    WARNING << "Could not identify file name in input " << name;
    return shared_ptr<line>();
  }

  string filename = name.substr(0, colon_pos);
  string line_no_str = name.substr(colon_pos + 1);

  size_t line_no;
  stringstream(line_no_str) >> line_no;

  for(const auto& f : files()) {
    string::size_type last_pos = f.first.rfind(filename);
    if(last_pos != string::npos && last_pos + filename.size() == f.first.size()) {
      if(f.second->has_line(line_no)) {
        return f.second->get_line(line_no);
      }
    }
  }

  return shared_ptr<line>();
}

shared_ptr<line> memory_map::find_line(uintptr_t addr) {
  auto iter = _ranges.find(addr);
  if(iter != _ranges.end()) {
    return iter->second;
  } else {
    return shared_ptr<line>();
  }
}

memory_map& memory_map::get_instance() {
  static char buf[sizeof(memory_map)];
  static memory_map* the_instance = new(buf) memory_map();
  return *the_instance;
}
