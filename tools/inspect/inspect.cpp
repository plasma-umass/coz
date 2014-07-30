#include <limits.h>
#include <unistd.h>

#include <iostream>
#include <vector>

#include "support.h"
#include "log.h"

using std::cerr;
using std::string;
using std::vector;

int main(int argc, char** argv) {
  if(argc != 2) {
    cerr << "Usage: " << argv[0] << " <path to ELF file>\n";
    return 2;
  }
  
  causal_support::memory_map m;
  
  vector<string> scope;
  // If the file scope is empty, add the current working directory
  char cwd[PATH_MAX];
  getcwd(cwd, PATH_MAX);
  scope.push_back(string(cwd));
  
  m.build(scope);
  
  size_t file_count = 0;
  size_t line_count = 0;
  for(const auto& f_info : m.files()) {
    const string& filename = f_info.first;
    file_count++;
    for(const auto& l_info : f_info.second->lines()) {
      line_count++;
      size_t line_number = l_info.first;
    }
  }
  
  INFO << "Found " << line_count << " lines in " << file_count << " files.";
  
  return 0;
}
