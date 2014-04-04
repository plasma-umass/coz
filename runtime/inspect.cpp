#include "inspect.h"

#include <cxxabi.h>
#include <elf.h>
#include <fcntl.h>
#include <link.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <cstdint>
#include <map>
#include <set>
#include <stack>
#include <string>

#include "arch.h"
#include "basic_block.h"
#include "causal.h"
#include "disassembler.h"
#include "log.h"

using namespace std;

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

/**
 * Locate and register all basic blocks with the profiler
 */
void registerBasicBlocks() {
  // Collect mapping information for all shared libraries
  map<uintptr_t, string> libs;
  dl_iterate_phdr(phdrCallback, reinterpret_cast<void*>(&libs));
  // Loop over libs
  for(const auto& e : libs) {
    // Check if each library should be included
    if(Causal::getInstance().includeFile(e.second) ) {
      INFO("Processing file %s", e.second.c_str());
      processELFFile(e.second, e.first);
    } else {
      INFO("Skipping file %s", e.second.c_str());
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
 * Open and map an ELF file, then process its function symbols
 */
void processELFFile(string path, uintptr_t loaded_base) {
  // Open the loaded file from disk
  int fd = ::open(path.c_str(), O_RDONLY);
  if(fd == -1) {
    WARNING("Failed to open file %s", path.c_str());
    return;
  }

  // Get file size
  struct stat sb;
  if(fstat(fd, &sb) == -1) {
    WARNING("Failed to get size for file %s", path.c_str());
    return;
  }
  
  // Map the ELF file
  ELFHeader* header = (ELFHeader*)mmap(nullptr, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if(header == MAP_FAILED) {
    WARNING("Failed to map file %s", path.c_str());
    return;
  }
  
  // Check for the ELF magic bytes
  if(!checkELFMagic(header)) {
    WARNING("Bad magic in file %s", path.c_str());
    return;
  }
  
  // Keep an integer representation of the file's base address for use with offsets
  uintptr_t file_base = (uintptr_t)header;
  
  ELFSectionHeader* sections = (ELFSectionHeader*)(file_base + header->e_shoff);
  
  // Validate the section header size
  REQUIRE(header->e_shentsize == sizeof(ELFSectionHeader), 
    "ELF section header size does not match loaded file");

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
      REQUIRE(section.sh_entsize == sizeof(ELFSymbol), "ELF symbol size does not match loaded file");
 
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
    WARNING("Failed to unmap ELF file");

  // Close the ELF file
  if(close(fd) == -1)
    WARNING("Failed to close ELF file");
}

/**
 * Validate an ELF file's magic bytes
 */
bool checkELFMagic(ELFHeader* header) {
  return header->e_ident[0] == 0x7f && header->e_ident[1] == 'E' &&
    header->e_ident[2] == 'L' && header->e_ident[3] == 'F';
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
          WARNING("Unhandled dynamic branch target in %s", fn->getName().c_str());
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
      Causal::getInstance().addBlock(new basic_block(fn, index, block_range));
      index++;
    }
    prev_base = base;
  }
  
  // Add the final basic block that adds at the function's limit address
  interval block_range(prev_base, loaded.getLimit());
  Causal::getInstance().addBlock(new basic_block(fn, index, block_range));
}
