#include <iostream>

#include "support.h"
#include "log.h"

using std::cerr;
using std::string;

int main(int argc, char** argv) {
  if(argc != 2) {
    cerr << "Usage: " << argv[0] << " <path to ELF file>\n";
    return 2;
  }
  
  causal_support::memory_map m;
  REQUIRE(m.process_file(argv[1])) << "Couldn't find a debug version of " << argv[1];
  
  for(const auto& f_info : m.files()) {
    const string& filename = f_info.first;
    for(const auto& l_info : f_info.second->lines()) {
      size_t line_number = l_info.first;
      INFO << l_info.second;
    }
  }
  
  return 0;
}
