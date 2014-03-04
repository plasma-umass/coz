#include "inspect.h"

#include <elf.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <cstdint>
#include <set>
#include <string>

#include "arch.h"
#include "basic_block.h"
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
void processELFFile(string path, uintptr_t base, uintptr_t limit);
bool checkELFMagic(ELFHeader* header);
void processFunction(string path, string func_name, uintptr_t base, uintptr_t limit);

/**
 * Locate and register all basic blocks with the profiler
 */
void registerBasicBlocks(set<string> scope) {
  readMappings(scope);
}

/**
 * Read /proc/self/maps to locate all mapped regions of memory
 */
void readMappings(set<string> scope) {
  FILE* map = fopen("/proc/self/maps", "r");

  int rc; 
  do {
    uintptr_t base, limit;
    char perms[4];
    size_t offset;
    uint8_t dev_major, dev_minor;
    int inode;
    char path[512];

    rc = fscanf(map, "%lx-%lx %s %lx %hhx:%hhx %d %s\n",
      &base, &limit, perms, &offset, &dev_major, &dev_minor, &inode, path);

    if(rc == 8 && perms[2] == 'x') {
      // Found an executable mapping! Process it if it matches any of the scope
      // patterns
      string path_string(path);
      for(const string& pat : scope) {
        fprintf(stderr, "%s: %s\n", pat.c_str(), path_string.c_str());
        if(path_string.find(pat) != string::npos) {
          processELFFile(path_string, base, limit);
          break;
        }
      }
    }
  } while(rc == 8);

  fclose(map);
}

/**
 * Open and map an ELF file, then process its symbols
 */
void processELFFile(string path, uintptr_t base, uintptr_t limit) {
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
  ELFHeader* header = (ELFHeader*)mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
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
          
          if(header->e_type == ET_DYN) {
            processFunction(path, fn_name, base + symbol.st_value, 
              base + symbol.st_value + symbol.st_size);
          } else {
            processFunction(path, fn_name, symbol.st_value, symbol.st_value + symbol.st_size);
          }
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

void processFunction(string path, string fn_name, uintptr_t base, uintptr_t limit) {
  fprintf(stderr, "Found function %s at %p\n", fn_name.c_str(), (void*)base);
}
