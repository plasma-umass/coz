#if !defined(CAUSAL_RUNTIME_ELF_H)
#define CAUSAL_RUNTIME_ELF_H

#include <elf.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <map>

#include "arch.h"
#include "interval.h"
#include "log.h"
#include "util.h"

using std::map;
using std::pair;

// We're going to examine libraries linked with the currently running program, so the architecture
// is always going to be the same. Define header, section header, and symbol types for convenience.
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

class ELFFile {
private:
  int _fd;
  size_t _size;
  ELFHeader* _header;
  ELFFile(int fd, size_t size, ELFHeader* header) : _fd(fd), _size(size), _header(header) {}
  
  template<typename T> T* getData(ptrdiff_t offset) const {
    return reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(_header) + offset);
  }
  
public:
  /// Delete the copy constructor
  ELFFile(const ELFFile&) = delete;
  
  /// Clean up the memory mapped file
  ~ELFFile() {
    if(munmap(_header, _size) == -1)
      WARNING("Failed to unmap ELF file");
    if(close(_fd) == -1)
      WARNING("Failed to close ELF file");
  }
  
  /// Is this a dynamic shared library?
  bool isDynamic() const {
    return _header->e_type == ET_DYN;
  }
  
  std::map<std::string, interval> getFunctions() const {
    std::map<std::string, interval> functions;
    
    ELFSectionHeader* sections = getData<ELFSectionHeader>(_header->e_shoff);
    REQUIRE(_header->e_shentsize == sizeof(ELFSectionHeader), 
      "ELF section header size does not match loaded file");

    // Get the number of section headers
    size_t section_count = _header->e_shnum;
    if(section_count == 0)
      section_count = sections->sh_size;

    // Loop over section headers
    for(ELFSectionHeader& section : wrap(sections, section_count)) {
      // Is this a symbol table section?
      if(section.sh_type == SHT_SYMTAB || section.sh_type == SHT_DYNSYM) {
        // Get the corresponding string table section header
        ELFSectionHeader& strtab_section = sections[section.sh_link];
        const char* strtab = getData<const char>(strtab_section.sh_offset);
   
        // Validate the symbol entry size
        REQUIRE(section.sh_entsize == sizeof(ELFSymbol), "ELF symbol size does not match loaded file");
   
        // Get the base pointer to this section's data
        ELFSymbol* symbols = getData<ELFSymbol>(section.sh_offset);
   
        // Loop over symbols in this section
        for(ELFSymbol& symbol : wrap(symbols, section.sh_size / sizeof(ELFSymbol))) {
          // Only handle function symbols with a defined value
          if(ELFSymbolType(symbol.st_info) == STT_FUNC && symbol.st_value != 0) {
            const char* name = strtab + symbol.st_name;
            functions[name] = interval(symbol.st_value, symbol.st_value + symbol.st_size);
          }
        }
      }
    }
    
    return functions;
  }
  
  static ELFFile* open(std::string filename) {
    // Open the loaded file from disk
    int fd = ::open(filename.c_str(), O_RDONLY);
    if(fd == -1) {
      WARNING("Failed to open file %s", filename.c_str());
      return NULL;
    }
  
    // Get file size
    struct stat sb;
    if(fstat(fd, &sb) == -1) {
      WARNING("Failed to get size for file %s", filename.c_str());
      return NULL;
    }
    
    // Map the ELF file
    ELFHeader* header = (ELFHeader*)mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if(header == MAP_FAILED) {
      WARNING("Failed to map file %s", filename.c_str());
      return NULL;
    }
    
    if(header->e_ident[0] != 0x7f || header->e_ident[1] != 'E' ||
       header->e_ident[2] != 'L' || header->e_ident[3] != 'F') {
      WARNING("Not an elf file. Found magic bytes %#x %c%c%c",
        header->e_ident[0], header->e_ident[1], header->e_ident[2], header->e_ident[3]);
      return NULL;
    }
    
    return new ELFFile(fd, sb.st_size, header);
  }
};

#endif
