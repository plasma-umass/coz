# CMake generated Testfile for 
# Source directory: /Users/emery/git/coz-portage/tests
# Build directory: /Users/emery/git/coz-portage/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(dwarf_scope_filter "/Users/emery/git/coz-portage/tests/run_dwarf_scope_test.sh" "/Users/emery/git/coz-portage/libcoz/libcoz.so" "/Users/emery/git/coz-portage/tests/dwarf_scope_test" "/Users/emery/git/coz-portage/tests/dwarf_scope")
set_tests_properties(dwarf_scope_filter PROPERTIES  ENVIRONMENT "PYTHONUNBUFFERED=1" _BACKTRACE_TRIPLES "/Users/emery/git/coz-portage/tests/CMakeLists.txt;17;add_test;/Users/emery/git/coz-portage/tests/CMakeLists.txt;0;")
