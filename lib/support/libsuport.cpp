#include "support.h"

#include <cxxabi.h>
#include <elf.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <link.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/filesystem.hpp>

#include "dwarf++.hh"
#include "elf++.hh"
#include "log.h"

using boost::is_any_of;
using boost::split;

using boost::filesystem::canonical;
using boost::filesystem::exists;
using boost::filesystem::path;

using std::ios;
using std::map;
using std::pair;
using std::shared_ptr;
using std::string;
using std::stringstream;
using std::vector;

/// Path for the main executable, as passed to exec()
extern "C" char* __progname_full;

namespace causal_support {
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
  
  /**
   * Get the full path to a file specified via absolute path, relative path, or raw name
   * resolved via the PATH variable.
   */
  static const string get_full_path(const string filename) {
    if(filename[0] == '/') {
      return filename;
    
    } else if(filename[0] == '.') {
      return canonical(filename).string();
    
    } else {  
      // Search the environment's path for the first match
      const string path_env = getenv("PATH");
      vector<string> search_dirs;
      split(search_dirs, path_env, is_any_of(":"));
    
      for(const string& dir : search_dirs) {
        auto p = path(dir) / filename;
        if(exists(p)) {
          return p.string();
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
  static elf::elf locate_debug_executable(const string filename) {
    elf::elf f;
  
    const string full_path = get_full_path(filename);
  
    // If a full path wasn't found, return the invalid ELF file
    if(full_path.length() == 0) {
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
  
    // If there isn't a .debug_info section, check for the .gnu_debuglink section
    auto& link_section = f.get_section(".gnu_debuglink");

    // Store the full path to the executable and its directory name
    string directory = full_path.substr(0, full_path.find_last_of('/'));

    // Build a vector of paths to search for a debug version of the file
    vector<string> search_paths;
  
    // Check for a build-id section
    string build_id = find_build_id(f);
    if(build_id.length() > 0) {
      string prefix = build_id.substr(0, 2);
      string suffix = build_id.substr(2);
    
      auto p = path("/usr/lib/debug/.build-id") / prefix / (suffix + ".debug");
      search_paths.push_back(p.string());
    }
  
    // Check for a debug_link section
    if(link_section.valid()) {
      string link_name = reinterpret_cast<const char*>(link_section.data());
    
      search_paths.push_back(directory + "/" + link_name);
      search_paths.push_back(directory + "/.debug/" + link_name);
      search_paths.push_back("/usr/lib/debug" + directory + "/" + link_name);
    }

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

  map<string, uintptr_t> get_loaded_files() {
    map<string, uintptr_t> result;
  
    // Walk through the loaded libraries
    dl_iterate_phdr([](struct dl_phdr_info* info, size_t sz, void* data) {
      map<string, uintptr_t>& result = *reinterpret_cast<map<string, uintptr_t>*>(data);
      if(result.size() == 0) {
        // The first entry is the main executable, which doesn't include a name.
        // Use the __progname_full constant instead.
        result[string(__progname_full)] = info->dlpi_addr;
      } else {
        // The rest of the entries will include both a name and load address
        result[string(info->dlpi_name)] = info->dlpi_addr;
      }
      return 0;
    }, (void*)&result);
  
    return result;
  }

  bool memory_map::process_file(const string& name, uintptr_t load_address) {
    elf::elf f = locate_debug_executable(name);
    
    // If a debug version of the file could not be located, return false
    if(!f.valid()) {
      return false;
    }
    
    // Read the DWARF information from the chosen file
    dwarf::dwarf d(dwarf::elf::create_loader(f));
    
    // Walk through the compilation units (source files) in the executable
    for(auto unit : d.compilation_units()) {
      string prev_filename;
      size_t prev_line;
      uintptr_t prev_address = 0;
      // Walk through the line instructions in the DWARF line table
      for(auto& line_info : unit.get_line_table()) {
        // Insert an entry if this isn't the first line command in the sequence
        if(prev_address != 0) {
          // Get or create the file handle for this entry
          shared_ptr<file> f = get_file(prev_filename);
          // Get or create the line handle for this entry
          shared_ptr<line> l = f->get_line(prev_line);
          // Make the memory range that holds this line
          interval range = interval(prev_address, line_info.address) + load_address;
          
          auto iter = _ranges.find(range);
          if(iter != _ranges.end() && iter->second != l) {
            WARNING << "Overlapping entries for lines " << f->get_name() << ":" << l->get_line()
              << " and " << iter->second->get_file()->get_name() << ":" << iter->second->get_line();
          }
          
          // Add the entry
          _ranges.emplace(range, l);
        }
      
        if(line_info.end_sequence) {
          prev_address = 0;
        } else {
          prev_filename = line_info.file->path;
          prev_line = line_info.line;
          prev_address = line_info.address;
        }
      }
    }
    
    return true;
  }
  
  shared_ptr<line> memory_map::find_line(const string& name) {
    WARNING << "Line name searches are not yet implemented!";
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
}
