#if !defined(CAUSAL_RUNTIME_OPTIONS_H)
#define CAUSAL_RUNTIME_OPTIONS_H

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <boost/program_options.hpp>

namespace causal {
  static boost::program_options::options_description get_options() {
    // Set up causal argument handling
    boost::program_options::options_description desc("Causal options");
    desc.add_options()
      ("help,h", "show this help message")
      ("exclude-main,e", "do not profile the main executable")
      ("include,i", 
        boost::program_options::value<std::vector<std::string>>()
          ->default_value(std::vector<std::string>(), ""),
        "profile libraries with matching names")
      ("fixed-line,l", 
        boost::program_options::value<std::string>()->default_value(""),
        "profile with a fixed source line as the optimization candidate (<file>:<line>)")
      ("fixed-speedup,s",
        boost::program_options::value<int>()->default_value(-1),
        "profile with a fixed speedup percent (0-100)")
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
      << "\t" << "causal causal_args... --- <program> args...\n"
      << get_options();
  }
  
  static boost::program_options::variables_map parse_args(size_t argc, char** argv) {
    // Parse the causal command line arguments
    boost::program_options::variables_map args;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, get_options()), args);
    boost::program_options::notify(args);
    
    return args;
  }
}

#endif
