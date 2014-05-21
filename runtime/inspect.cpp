#include "inspect.h"

#include <cxxabi.h>
#include <elf.h>
#include <fcntl.h>
#include <link.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <map>
#include <set>
#include <stack>
#include <stdexcept>
#include <string>

#include "arch.h"
#include "basic_block.h"
#include "disassembler.h"
#include "log.h"
#include "profiler.h"

using std::map;
using std::pair;
using std::set;
using std::stack;
using std::string;

// The executable/library must match the current bittedness, so typedef appropriately
_X86(typedef Elf32_Ehdr ELFHeader);
_X86(typedef Elf32_Shdr ELFSectionHeader);
_X86(typedef Elf32_Sym ELFSymbol);
_X86_64(typedef Elf64_Ehdr ELFHeader);
_X86_64(typedef Elf64_Shdr ELFSectionHeader);
_X86_64(typedef Elf64_Sym ELFSymbol);

#if _IS_X86
# define ELFSymbolType(x) ELF32_ST_TYPE(x)
#else
# define ELFSymbolType(x) ELF64_ST_TYPE(x)
#endif

void readMappings(set<string> scope);
int phdrCallback(struct dl_phdr_info* info, size_t sz, void* data);
void processELFFile(string path, uintptr_t loaded);
bool checkELFMagic(ELFHeader* header);
void processFunction(string path, string func_name, interval loaded);

/// Path for the main executable, as passed to exec()
extern "C" char* __progname_full;

/// A map of functions by mangled name
map<string, function_info*> mangled_functions;

/// A map of functions by demangled name
map<string, function_info*> demangled_functions;

/// A map of basic blocks in the processed executables
map<interval, basic_block*> blocks;

/**
 * Locate and register all basic blocks with the profiler
 */
void inspectExecutables(set<string> patterns, bool include_all) {
  // Collect mapping information for all shared libraries
  map<uintptr_t, string> libs;
  dl_iterate_phdr(phdrCallback, reinterpret_cast<void*>(&libs));
  
  // Loop over libs
  for(const auto& e : libs) {
    bool include = include_all;
    
    // Loop through patterns to check for a match
    auto iter = patterns.begin();
    while(!include && iter != patterns.end()) {
      if(e.second.find(*iter) != string::npos) {
        include = true;
      }
      iter++;
    }
    
    // If matched, process the file
    if(include) {
      INFO << "Processing file " << e.second;
      processELFFile(e.second, e.first);
    }
  }
}

/**
 * Callback invoked for each loaded binary or library
 */
int phdrCallback(struct dl_phdr_info* info, size_t sz, void* data) {
  map<uintptr_t, string>& libs = *reinterpret_cast<map<uintptr_t, string>*>(data);
  if(libs.size() == 0) {
    // Get the full path to the main executable
    char* main_exe = realpath(__progname_full, nullptr);
    // The first callback will pass the relocation of the main executable
    libs[info->dlpi_addr] = string(main_exe);
    // Free the full path
    free(main_exe);
  } else {
    libs[info->dlpi_addr] = string(info->dlpi_name);
  }
  return 0;
}

/**
 * Find a block by address
 */
basic_block* findBlock(uintptr_t p) {
  auto iter = blocks.find(p);
  if(iter != blocks.end()) {
    return iter->second;
  } else {
    return nullptr;
  }
}

/**
 * Find a symbol by name, either mangled or demangled
 */
function_info* findSymbol(string s) {
  // Check for the symbol in the mangled functions map
  auto iter = mangled_functions.find(s);
  if(iter != mangled_functions.end()) {
    return iter->second;
  }
  // Check for the symbol in the demangled functions map
  iter = demangled_functions.find(s);
  if(iter != demangled_functions.end()) {
    return iter->second;
  }
  // Symbol not found
  return nullptr;
}

/**
 * Find a basic block by name. There are three valid formats:
 *  - <symbol_name>: returns the first block in the matching symbol
 *  - <symbol_name>+<offset>: returns the block containing the specified address
 *  - <symbol_name>:<block_index>: returns the block_index-th basic block in the function
 */
basic_block* findBlock(string s) {
  // First, check if the argument is a symbol on its own
  function_info* sym = findSymbol(s);
  if(sym != nullptr) {
    auto iter = blocks.find(sym->getInterval().getBase());
    if(iter != blocks.end()) {
      return iter->second;
    }
  }
  
  // Second, treat the block as a symbol+offset format:
  size_t split_index = s.find_last_of('+');
  if(split_index != string::npos) {
    sym = findSymbol(s.substr(0, split_index));
    if(sym != nullptr) {
      try {
        size_t offset = std::stoul(s.substr(split_index+1));
        uintptr_t addr = sym->getInterval().getBase() + offset;
        auto iter = blocks.find(addr);
        if(iter != blocks.end()) {
          return iter->second;
        }
      } catch(std::invalid_argument& e) {
        // Move on
      }
    }
  }
  
  // If that didn't work, try symbol:block_index format:
  split_index = s.find_last_of(':');
  if(split_index != string::npos) {
    sym = findSymbol(s.substr(0, split_index));
    if(sym != nullptr) {
      try {
        size_t block_index = std::stoul(s.substr(split_index+1));
        // Get the first block in the function
        auto iter = blocks.find(sym->getInterval().getBase());
        // Advance through the blocks map until the requested block is found, the end
        // of the map is reached, or we start seeing blocks in a different symbol
        size_t i = block_index;
        while(i > 0 && iter != blocks.end() && sym->getInterval().contains(iter->first.getBase())) {
          i--;
          iter++;
        }
        // If the loop finished and we didn't reach the end of the map, return the block
        if(i == 0 && iter != blocks.end()) {
          return iter->second;
        }
      } catch(std::invalid_argument& e) {
        // Move on
      }
    }
  }

  // No block found
  return nullptr;
}

const map<interval, basic_block*>& getBlocks() {
  return blocks;
}

/**
 * Open and map an ELF file, then process its function symbols
 */
void processELFFile(string path, uintptr_t loaded_base) {
  // Open the loaded file from disk
  int fd = ::open(path.c_str(), O_RDONLY);
  if(fd == -1) {
    WARNING << "Failed to open file " << path;
    return;
  }

  // Get file size
  struct stat sb;
  if(fstat(fd, &sb) == -1) {
    WARNING << "Failed to get size for file " << path;
    return;
  }
  
  // Map the ELF file
  ELFHeader* header = (ELFHeader*)mmap(nullptr, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if(header == MAP_FAILED) {
    WARNING << "Failed to map file " << path;
    return;
  }
  
  // Check for the ELF magic bytes
  if(!checkELFMagic(header)) {
    WARNING << "Bad magic in file " << path;
    return;
  }
  
  // Keep an integer representation of the file's base address for use with offsets
  uintptr_t file_base = (uintptr_t)header;
  
  ELFSectionHeader* sections = (ELFSectionHeader*)(file_base + header->e_shoff);
  
  // Validate the section header size
  REQUIRE(header->e_shentsize == sizeof(ELFSectionHeader))
      << "ELF section header size does not match loaded file";

  // Get the number of section headers
  size_t section_count = header->e_shnum;
  if(section_count == 0)
    section_count = sections->sh_size;

  // Loop over section headers
  for(int section_index = 0; section_index < section_count; section_index++) {
    // Get the current section header
    ELFSectionHeader& section = sections[section_index];
    // Is this a symbol table section?
    if(section.sh_type == SHT_SYMTAB || section.sh_type == SHT_DYNSYM) {
      // Get the corresponding string table section header
      ELFSectionHeader& strtab_section = sections[section.sh_link];
      const char* strtab = (const char*)(file_base + strtab_section.sh_offset);
 
      // Validate the symbol entry size
      REQUIRE(section.sh_entsize == sizeof(ELFSymbol)) 
          << "ELF symbol size does not match loaded file";
 
      // Get the base pointer to this section's data
      ELFSymbol* symbols = (ELFSymbol*)(file_base + section.sh_offset);
 
      // Calculate the number of symbols
      int symbol_count = section.sh_size / sizeof(ELFSymbol);
      
      // Loop over symbols in this section
      for(int symbol_index = 0; symbol_index < symbol_count; symbol_index++) {
        ELFSymbol& symbol = symbols[symbol_index];
        
        // Only handle function symbols with a defined value
        if(ELFSymbolType(symbol.st_info) == STT_FUNC && symbol.st_value != 0) {
          const char* fn_name = strtab + symbol.st_name;

          interval fn_loaded(symbol.st_value, symbol.st_value + symbol.st_size);  
          
          // Adjust the function load address for dynamic shared libraries
          if(header->e_type == ET_DYN)
            fn_loaded += loaded_base;
          
          // Process the function
          processFunction(path, fn_name, fn_loaded);
        }
      }
    }
  }
  
  // Unmap the ELF file
  if(munmap(header, sb.st_size) == -1)
    WARNING << "Failed to unmap ELF file";

  // Close the ELF file
  if(close(fd) == -1)
    WARNING << "Failed to close ELF file";
}

/**
 * Validate an ELF file's magic bytes
 */
bool checkELFMagic(ELFHeader* header) {
  return header->e_ident[0] == 0x7f && header->e_ident[1] == 'E' &&
    header->e_ident[2] == 'L' && header->e_ident[3] == 'F';
}

/**
 * Insert a function into the map of functions
 */
void registerFunction(function_info* fn) {
  mangled_functions.insert(pair<string, function_info*>(fn->getRawSymbolName(), fn));
  demangled_functions.insert(pair<string, function_info*>(fn->getName(), fn));
}

/**
 * Insert a block into the map of basic blocks
 */
void registerBasicBlock(basic_block* block) {
  blocks.insert(pair<interval, basic_block*>(block->getInterval(), block));
}

/**
 * Process a function symbol with a known load address
 */
void processFunction(string path, string fn_name, interval loaded) {
  // Get a demangled version of the function name
  string demangled;
  char* demangled_cstr = abi::__cxa_demangle(fn_name.c_str(), nullptr, nullptr, nullptr);
  if(demangled_cstr == nullptr) {
    demangled = fn_name;
  } else {
    demangled = demangled_cstr;
  }
  
  // Allocate a function info object for basic blocks to share
  function_info* fn = new function_info(path, fn_name, demangled, loaded);
  
  // Register the function with the profiler
  registerFunction(fn);
  
  // Disassemble to find starting addresses of all basic blocks
  set<uintptr_t> block_bases;
  stack<uintptr_t> q;
  q.push(loaded.getBase());

  while(q.size() > 0) {
    uintptr_t p = q.top();
    q.pop();

    // Skip null or already-seen pointers
    if(p == 0 || block_bases.find(p) != block_bases.end())
      continue;

    // This is a new block starting address
    block_bases.insert(p);

    disassembler i(p, loaded.getLimit());
    bool block_ended = false;
    do {
      // Any branch ends a basic block
      if(i.branches()) {
        block_ended = true;
        
        // If the block falls through, start a new block at the next instruction
        if(i.fallsThrough())
          q.push(i.limit());
        
        // Add the branch target
        branch_target target = i.target();
        if(target.dynamic()) {
          // TODO: Finish processing basic blocks, then just include all uncovered
          // memory as a (probably inaccurate) "basic block"
          WARNING << "Unhandled dynamic branch target in " << fn->getName();
        } else {
          uintptr_t t = target.value();
          
          if(loaded.contains(t))
            q.push(t);
        }
      }
      
      i.next();
    } while(!block_ended && !i.done());
  }
  
  // Create basic block objects and register them with the profiler
  size_t index = 0;
  uintptr_t prev_base = 0;
  for(uintptr_t base : block_bases) {
    if(prev_base != 0) {
      interval block_range(prev_base, base);
      registerBasicBlock(new basic_block(fn, index, block_range));
      index++;
    }
    prev_base = base;
  }
  
  // Add the final basic block that adds at the function's limit address
  interval block_range(prev_base, loaded.getLimit());
  registerBasicBlock(new basic_block(fn, index, block_range));
}
