#if !defined(CAUSAL_RUNTIME_OPTIONS_H)
#define CAUSAL_RUNTIME_OPTIONS_H

#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <boost/program_options.hpp>

namespace causal {
  static boost::program_options::options_description get_options() {
    // Set up causal argument handling
    boost::program_options::options_description desc("coz options");
    desc.add_options()
      ("help,h", "show this help message")
        
      ("scope,s", 
        boost::program_options::value<std::vector<std::string>>()
          ->default_value(std::vector<std::string>(), ""),
        "evaluate optimizations to files in the specified file/directory")
          
      ("search-libs", "search shared libraries for in-scope source files")
          
      ("sample-only", "run the profiler without any virtual speedups")
          
      ("line", 
        boost::program_options::value<std::string>()->default_value(""),
        "profile with a fixed source line as the optimization candidate (<file>:<line>)")
          
      ("speedup",
        boost::program_options::value<int>()->default_value(-1),
        "profile with a fixed speedup percent (0-100)")
          
      ("end-to-end,e", "run a single performance experiment for the whole program execution")
          
      ("progress,p", 
        boost::program_options::value<std::vector<std::string>>()
          ->default_value(std::vector<std::string>(), ""),
        "additional progress points (<file>:<line>)")
          
      ("output,o", 
        boost::program_options::value<std::string>()
          ->default_value("profile.log"),
        "profile output filename");
    
    return desc;
  }
  
  static void show_usage() {
    std::cerr << "Usage:\n"
      << "\t" << "coz coz_args ... --- <program> args...\n"
      << get_options();
  }
  
  static boost::program_options::variables_map parse_args(size_t argc, char** argv) {
    // Parse the coz command line arguments
    boost::program_options::variables_map args;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, get_options()), args);
    boost::program_options::notify(args);
    
    return args;
  }
}

#endif
