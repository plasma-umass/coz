#include <elf.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>
#include <sstream>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/filesystem.hpp>

#include "dwarf++.hh"
#include "elf++.hh"
#include "interval.h"
#include "log.h"

using namespace std;
using namespace boost;
using namespace boost::filesystem;

string find_build_id(elf::elf& f) {
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

const string get_full_path(const string filename) {
  if(filename[0] == '/') {
    return filename;
    
  } else if(filename[0] == '.') {
    return canonical(filename).string();
    
  } else {  
    // Search the environment's path for the first match
    const string path_env = getenv("PATH");
    vector<string> search_dirs;
    split(search_dirs, path_env, boost::is_any_of(":"));
    
    for(const string& dir : search_dirs) {
      auto p = path(dir) / filename;
      if(exists(p)) {
        return p.string();
      } else {
        INFO << p.string() << " does not exist...";
      }
    }
  }
  
  return "";
}

elf::elf locate_debug_executable(const string filename) {
  elf::elf f;
  
  const string full_path = get_full_path(filename);
  
  // If a full path wasn't found, return the invalid ELF file
  if(full_path.length() == 0) {
    return f;
  }
  
  INFO << "Opening " << full_path;
  
  int fd = open(full_path.c_str(), O_RDONLY);
  
  // If the file couldn't be opened, return the invalid ELF file
  if(fd < 0) {
    return f;
  }
  
  INFO << "Opened " << full_path;
  
  // Load the opened ELF file
  f = elf::elf(elf::create_mmap_loader(fd));
  
  INFO << "Loaded";
  
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
    INFO << "Trying " << path;
    fd = open(path.c_str(), O_RDONLY);
    if(fd >= 0) {
      f = elf::elf(elf::create_mmap_loader(fd));
      if(f.get_section(".debug_info").valid()) {
        INFO << "Found debug symbols in " << path;
        break;
      }
      f = elf::elf();
    }
  }
  
  return f;
}

int main(int argc, char** argv) {
  if(argc != 2) {
    cerr << "Usage: " << argv[0] << " <path to ELF file>\n";
    return 2;
  }
  
  elf::elf f = locate_debug_executable(argv[1]);
  
  REQUIRE(f.valid()) << "Couldn't find a debug version of " << argv[1];
  
  dwarf::dwarf d(dwarf::elf::create_loader(f));
  
  map<string, map<size_t, vector<interval>>> line_map;
  
  for(auto unit : d.compilation_units()) {
    string prev_file;
    size_t prev_line;
    uintptr_t prev_address = 0;
    for(auto& line : unit.get_line_table()) {
      if(prev_address != 0) {
        vector<interval>& intervals = line_map[prev_file][prev_line];
        
        if(intervals.size() > 0 && intervals.back().getLimit() == prev_address) {
          prev_address = intervals.back().getBase();
          intervals.pop_back();
        }
        
        line_map[prev_file][prev_line].push_back(interval(prev_address, line.address));
      }
      
      if(line.end_sequence) {
        prev_address = 0;
      } else {
        prev_file = line.file->path;
        prev_line = line.line;
        prev_address = line.address;
      }
    }
  }
  
  map<interval, pair<string, size_t>> inverted;
  
  for(const auto& file : line_map) {
    cout << file.first << "\n";
    for(const auto& line : file.second) {
      cout << "    " << line.first << ": ";
      for(const auto& range : line.second) {
        auto iter = inverted.find(range);
        if(iter != inverted.end()) {
          PREFER(iter->second.first == file.first && iter->second.second == line.first)
            << "New line information for:\n"
            << "  " << file.first << ":" << line.first << " (" << range << ")\n"
            << "  overlaps with entry for "
            << iter->second.first << ":" << iter->second.second << " (" << iter->first << ")";
        }
        
        inverted[range] = pair<string, size_t>(file.first, line.first); 
        
        cout << hex << range << "  " << dec;
      }
      cout << "\n";
    }
    cout << "\n";
  }
  
  return 0;
}
