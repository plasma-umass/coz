#ifdef __APPLE__

#include "macho_support.h"

#include <fcntl.h>
#include <libkern/OSByteOrder.h>
#include <mach-o/fat.h>
#include <mach-o/loader.h>
#include <mach-o/dyld.h>
#include <mach/machine.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <spawn.h>
#include <sys/wait.h>

#include <dwarf++.hh>

extern char** environ;

namespace macho_support {
namespace {

struct section_view {
  const void* data;
  size_t size;
};

class macho_dwarf_loader : public dwarf::loader {
public:
  macho_dwarf_loader(std::shared_ptr<void> mapping,
                     std::map<dwarf::section_type, section_view> sections)
      : _mapping(std::move(mapping)),
        _sections(std::move(sections)) {}

  const void* load(dwarf::section_type section, size_t* size_out) override {
    auto it = _sections.find(section);
    if(it == _sections.end())
      return nullptr;
    if(size_out)
      *size_out = it->second.size;
    return it->second.data;
  }

private:
  std::shared_ptr<void> _mapping;
  std::map<dwarf::section_type, section_view> _sections;
};

template <typename Enum>
struct has_line_str {
  template <typename T>
  static auto test(int) -> decltype(T::line_str, std::true_type());
  template <typename>
  static auto test(...) -> std::false_type;
  static constexpr bool value = decltype(test<Enum>(0))::value;
};
static_assert(has_line_str<dwarf::section_type>::value,
              "libelfin is expected to expose section_type::line_str");

template <typename Enum>
bool assign_line_str(Enum* out, const char* suffix, std::true_type) {
  if(std::strcmp(suffix, "line_str") == 0) {
    *out = Enum::line_str;
    return true;
  }
  return false;
}

template <typename Enum>
bool assign_line_str(Enum*, const char*, std::false_type) {
  return false;
}

struct mapped_file {
  std::shared_ptr<void> mapping;
  size_t size = 0;
};

mapped_file map_file(const std::string& path) {
  mapped_file result;
  int fd = open(path.c_str(), O_RDONLY);
  if(fd < 0)
    return result;

  struct stat st {};
  if(fstat(fd, &st) != 0) {
    close(fd);
    return result;
  }

  if(st.st_size == 0) {
    close(fd);
    return result;
  }

  void* addr = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if(addr == MAP_FAILED)
    return result;

  size_t length = static_cast<size_t>(st.st_size);
  auto deleter = [length](void* ptr) {
    if(ptr && ptr != MAP_FAILED) {
      munmap(ptr, length);
    }
  };

  result.mapping.reset(addr, deleter);
  result.size = length;
  return result;
}

cpu_type_t preferred_cpu_type() {
  if(const mach_header* header = _dyld_get_image_header(0)) {
    return header->cputype;
  }
#if defined(__aarch64__) || defined(__arm64__)
  return CPU_TYPE_ARM64;
#elif defined(__x86_64__) || defined(__x86_64)
  return CPU_TYPE_X86_64;
#else
  return 0;
#endif
}

cpu_subtype_t preferred_cpu_subtype(cpu_type_t type) {
  if(const mach_header* header = _dyld_get_image_header(0)) {
    return header->cpusubtype;
  }
#if defined(__aarch64__) || defined(__arm64__)
  if(type == CPU_TYPE_ARM64)
    return CPU_SUBTYPE_ARM64_ALL;
#elif defined(__x86_64__) || defined(__x86_64)
  if(type == CPU_TYPE_X86_64)
    return CPU_SUBTYPE_X86_64_ALL;
#endif
  return CPU_SUBTYPE_MULTIPLE;
}

bool select_fat_slice(const fat_arch* archs,
                      uint32_t narch,
                      bool swap_bytes,
                      const uint8_t* base,
                      size_t file_size,
                      const mach_header_64** header_out,
                      size_t* remaining_out,
                      size_t* slice_offset_out) {
  cpu_type_t preferred = preferred_cpu_type();
  cpu_subtype_t preferred_subtype = preferred_cpu_subtype(preferred);
  const fat_arch* chosen = nullptr;

  for(uint32_t i = 0; i < narch; i++) {
    cpu_type_t cputype = swap_bytes ? OSSwapInt32(archs[i].cputype)
                                    : archs[i].cputype;
    cpu_subtype_t subtype = swap_bytes ? OSSwapInt32(archs[i].cpusubtype)
                                       : archs[i].cpusubtype;
    if(preferred == 0 || cputype == preferred) {
      // Mask out the flag bits (like CPU_SUBTYPE_LIB64) for comparison
      cpu_subtype_t subtype_masked = subtype & ~CPU_SUBTYPE_MASK;
      cpu_subtype_t preferred_masked = preferred_subtype & ~CPU_SUBTYPE_MASK;
      if(cputype == preferred &&
         (preferred_masked == CPU_SUBTYPE_MULTIPLE ||
          subtype_masked == preferred_masked)) {
        chosen = &archs[i];
        break;
      }
      if(!chosen) {
        chosen = &archs[i];
      }
    }
  }

  if(!chosen)
    return false;

  uint32_t offset = swap_bytes ? OSSwapInt32(chosen->offset) : chosen->offset;
  uint32_t size = swap_bytes ? OSSwapInt32(chosen->size) : chosen->size;

  if(offset + size > file_size || size < sizeof(mach_header_64))
    return false;

  const mach_header_64* header =
      reinterpret_cast<const mach_header_64*>(base + offset);
  if(header->magic != MH_MAGIC_64)
    return false;

  *header_out = header;
  *remaining_out = file_size - offset;
  if(slice_offset_out)
    *slice_offset_out = offset;
  return true;
}

bool select_fat_slice(const fat_arch_64* archs,
                      uint32_t narch,
                      bool swap_bytes,
                      const uint8_t* base,
                      size_t file_size,
                      const mach_header_64** header_out,
                      size_t* remaining_out,
                      size_t* slice_offset_out) {
  cpu_type_t preferred = preferred_cpu_type();
  cpu_subtype_t preferred_subtype = preferred_cpu_subtype(preferred);
  const fat_arch_64* chosen = nullptr;

  for(uint32_t i = 0; i < narch; i++) {
    cpu_type_t cputype = swap_bytes ? OSSwapInt32(archs[i].cputype)
                                    : archs[i].cputype;
    cpu_subtype_t subtype = swap_bytes ? OSSwapInt32(archs[i].cpusubtype)
                                       : archs[i].cpusubtype;
    if(preferred == 0 || cputype == preferred) {
      if(cputype == preferred &&
         (preferred_subtype == CPU_SUBTYPE_MULTIPLE ||
          subtype == preferred_subtype)) {
        chosen = &archs[i];
        break;
      }
      if(!chosen)
        chosen = &archs[i];
    }
  }

  if(!chosen)
    return false;

  uint64_t offset = swap_bytes ? OSSwapInt64(chosen->offset) : chosen->offset;
  uint64_t size = swap_bytes ? OSSwapInt64(chosen->size) : chosen->size;

  if(offset + size > file_size || size < sizeof(mach_header_64))
    return false;

  const mach_header_64* header =
      reinterpret_cast<const mach_header_64*>(base + offset);
  if(header->magic != MH_MAGIC_64)
    return false;

  *header_out = header;
  *remaining_out = file_size - offset;
  if(slice_offset_out)
    *slice_offset_out = static_cast<size_t>(offset);
  return true;
}

bool find_mach_header(const uint8_t* base,
                      size_t size,
                      const mach_header_64** header_out,
                      size_t* remaining_out,
                      size_t* slice_offset_out) {
  if(size < sizeof(uint32_t))
    return false;

  uint32_t magic = *reinterpret_cast<const uint32_t*>(base);
  switch(magic) {
    case MH_MAGIC_64:
      if(size < sizeof(mach_header_64))
        return false;
      *header_out = reinterpret_cast<const mach_header_64*>(base);
      *remaining_out = size;
      if(slice_offset_out)
        *slice_offset_out = 0;
      return true;
    case FAT_MAGIC:
    case FAT_CIGAM: {
      if(size < sizeof(fat_header))
        return false;
      const fat_header* header = reinterpret_cast<const fat_header*>(base);
      bool swap_bytes = (magic == FAT_CIGAM);
      uint32_t narch = swap_bytes ? OSSwapInt32(header->nfat_arch)
                                  : header->nfat_arch;
      if(size < sizeof(fat_header) + narch * sizeof(fat_arch))
        return false;
      const fat_arch* archs = reinterpret_cast<const fat_arch*>(header + 1);
      return select_fat_slice(archs,
                              narch,
                              swap_bytes,
                              base,
                              size,
                              header_out,
                              remaining_out,
                              slice_offset_out);
    }
    case FAT_MAGIC_64:
    case FAT_CIGAM_64: {
      if(size < sizeof(fat_header))
        return false;
      const fat_header* header = reinterpret_cast<const fat_header*>(base);
      bool swap_bytes = (magic == FAT_CIGAM_64);
      uint32_t narch = swap_bytes ? OSSwapInt32(header->nfat_arch)
                                  : header->nfat_arch;
      if(size < sizeof(fat_header) + narch * sizeof(fat_arch_64))
        return false;
      const fat_arch_64* archs =
          reinterpret_cast<const fat_arch_64*>(header + 1);
      return select_fat_slice(archs,
                              narch,
                              swap_bytes,
                              base,
                              size,
                              header_out,
                              remaining_out,
                              slice_offset_out);
    }
    default:
      return false;
  }
}

bool get_section_type(const char* sectname, dwarf::section_type* out) {
  if(std::strncmp(sectname, "__debug_", 8) != 0)
    return false;

  const char* suffix = sectname + 8;
  if(std::strcmp(suffix, "abbrev") == 0) *out = dwarf::section_type::abbrev;
  else if(std::strcmp(suffix, "aranges") == 0) *out = dwarf::section_type::aranges;
  else if(std::strcmp(suffix, "frame") == 0) *out = dwarf::section_type::frame;
  else if(std::strcmp(suffix, "info") == 0) *out = dwarf::section_type::info;
  else if(std::strcmp(suffix, "line") == 0) *out = dwarf::section_type::line;
  else if(std::strcmp(suffix, "loc") == 0) *out = dwarf::section_type::loc;
  else if(std::strcmp(suffix, "macinfo") == 0) *out = dwarf::section_type::macinfo;
  else if(std::strcmp(suffix, "pubnames") == 0) *out = dwarf::section_type::pubnames;
  else if(std::strcmp(suffix, "pubtypes") == 0) *out = dwarf::section_type::pubtypes;
  else if(std::strcmp(suffix, "ranges") == 0) *out = dwarf::section_type::ranges;
  else if(std::strcmp(suffix, "str") == 0) *out = dwarf::section_type::str;
  // Handle both full name and Mach-O truncated name (16 char limit)
  else if(std::strcmp(suffix, "str_offsets") == 0 || std::strcmp(suffix, "str_offs") == 0) *out = dwarf::section_type::str_offsets;
  else if(std::strcmp(suffix, "types") == 0) *out = dwarf::section_type::types;
  else if(assign_line_str(out,
                          suffix,
                          std::integral_constant<bool, has_line_str<dwarf::section_type>::value>()))
    return true;
  else
    return false;

  return true;
}

bool collect_dwarf_sections(const uint8_t* base,
                            size_t size,
                            std::map<dwarf::section_type, section_view>& sections) {
  const mach_header_64* header = nullptr;
  size_t remaining = 0;
  size_t slice_offset = 0;
  if(!find_mach_header(base, size, &header, &remaining, &slice_offset))
    return false;

  const uint8_t* command_ptr =
      reinterpret_cast<const uint8_t*>(header) + sizeof(mach_header_64);
  if(command_ptr < base || command_ptr > base + size)
    return false;

  for(uint32_t i = 0; i < header->ncmds; i++) {
    if(command_ptr + sizeof(load_command) > base + size)
      return false;
    const load_command* cmd =
        reinterpret_cast<const load_command*>(command_ptr);
    if(cmd->cmdsize == 0 || command_ptr + cmd->cmdsize > base + size)
      return false;

    if(cmd->cmd == LC_SEGMENT_64) {
      const segment_command_64* seg =
          reinterpret_cast<const segment_command_64*>(command_ptr);
      const section_64* secs =
          reinterpret_cast<const section_64*>(seg + 1);
      for(uint32_t s = 0; s < seg->nsects; s++) {
        const section_64& sec = secs[s];
        if(std::strncmp(sec.segname, "__DWARF", sizeof(sec.segname)) != 0)
          continue;
        if(sec.offset + sec.size > size)
          continue;
        dwarf::section_type type;
        char section_name[sizeof(sec.sectname) + 1];
        memcpy(section_name, sec.sectname, sizeof(sec.sectname));
        section_name[sizeof(sec.sectname)] = '\0';
        if(get_section_type(section_name, &type)) {
          const void* data = base + slice_offset + sec.offset;
          sections[type] = section_view{data, static_cast<size_t>(sec.size)};
        }
      }
    }

    command_ptr += cmd->cmdsize;
  }

  return !sections.empty();
}

std::shared_ptr<dwarf::loader> load_dwarf_from_file(const std::string& path) {
  mapped_file file = map_file(path);
  if(!file.mapping || file.size == 0)
    return nullptr;

  std::map<dwarf::section_type, section_view> sections;
  if(!collect_dwarf_sections(static_cast<const uint8_t*>(file.mapping.get()),
                             file.size,
                             sections)) {
    return nullptr;
  }

  return std::make_shared<macho_dwarf_loader>(file.mapping, std::move(sections));
}

} // namespace

std::string find_dsym_bundle(const std::string& binary_path) {
  auto slash = binary_path.find_last_of('/');
  std::string name = (slash == std::string::npos)
                       ? binary_path
                       : binary_path.substr(slash + 1);
  if(name.empty())
    return std::string();

  std::string bundle =
      binary_path + ".dSYM/Contents/Resources/DWARF/" + name;
  struct stat st {};
  if(stat(bundle.c_str(), &st) == 0 && S_ISREG(st.st_mode))
    return bundle;
  return std::string();
}

std::string basename_for(const std::string& path) {
  auto slash = path.find_last_of('/');
  if(slash == std::string::npos)
    return path;
  return path.substr(slash + 1);
}

std::string make_temp_dir() {
  std::string tmpl = "/tmp/coz-dsym-XXXXXX";
  std::vector<char> buffer(tmpl.begin(), tmpl.end());
  buffer.push_back('\0');
  char* created = mkdtemp(buffer.data());
  if(!created)
    return std::string();
  return std::string(created);
}

bool run_dsymutil(const std::string& binary_path, const std::string& bundle_path) {
  std::array<char*, 5> args {
    const_cast<char*>("dsymutil"),
    const_cast<char*>(binary_path.c_str()),
    const_cast<char*>("-o"),
    const_cast<char*>(bundle_path.c_str()),
    nullptr
  };

  std::vector<std::string> env_storage;
  std::vector<char*> envp;
  if(environ) {
    for(char** entry = environ; *entry != nullptr; ++entry) {
      if(std::strncmp(*entry, "DYLD_INSERT_LIBRARIES=", 22) == 0)
        continue;
      if(std::strncmp(*entry, "DYLD_FORCE_FLAT_NAMESPACE=", 26) == 0)
        continue;
      env_storage.emplace_back(*entry);
    }
  }
  for(auto& entry : env_storage) {
    envp.push_back(const_cast<char*>(entry.c_str()));
  }
  envp.push_back(nullptr);

  pid_t pid = 0;
  int rc = posix_spawnp(&pid, "dsymutil", nullptr, nullptr, args.data(), envp.data());
  if(rc != 0) {
    fprintf(stderr,
            "dsymutil spawn failed for %s: %s\n",
            binary_path.c_str(),
            strerror(rc));
    return false;
  }

  int status = 0;
  if(waitpid(pid, &status, 0) == -1) {
    fprintf(stderr, "waitpid failed for dsymutil (%s)\n", binary_path.c_str());
    return false;
  }
  if(!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    fprintf(stderr,
            "dsymutil exited with status %d for %s\n",
            status,
            binary_path.c_str());
    return false;
  }
  return true;
}

std::string generate_dsym_bundle(const std::string& binary_path) {
  static std::mutex mutex;
  static std::unordered_map<std::string, std::string> generated;

  {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = generated.find(binary_path);
    if(it != generated.end())
      return it->second;
  }

  std::string temp_dir = make_temp_dir();
  if(temp_dir.empty())
    return std::string();

  std::string base = basename_for(binary_path);
  std::string bundle = temp_dir + "/" + base + ".dSYM";
  if(!run_dsymutil(binary_path, bundle))
    return std::string();

  std::string dwarf = bundle + "/Contents/Resources/DWARF/" + base;
  {
    std::lock_guard<std::mutex> lock(mutex);
    generated.emplace(binary_path, dwarf);
  }
  return dwarf;
}

std::shared_ptr<dwarf::loader> load_debug_info(const std::string& binary_path) {
  std::string dsym = find_dsym_bundle(binary_path);

  if(dsym.empty()) {
    dsym = generate_dsym_bundle(binary_path);
  }
  if(!dsym.empty()) {
    auto loader = load_dwarf_from_file(dsym);
    if(loader) {
      return loader;
    }
  }

  return load_dwarf_from_file(binary_path);
}

} // namespace macho_support

#endif // __APPLE__
