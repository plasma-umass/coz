add_executable(dwarf_scope_test
  ${CMAKE_SOURCE_DIR}/tests/dwarf/dwarf_scope_test.cpp)
target_include_directories(dwarf_scope_test PRIVATE
  ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(dwarf_scope_test PRIVATE Threads::Threads)
target_compile_features(dwarf_scope_test PRIVATE cxx_std_11)
if(APPLE)
  # macOS toolchains already default to DWARF-4; forcing DWARF-5 causes
  # dsymutil to emit forms libelfin can't parse yet.
  target_compile_options(dwarf_scope_test PRIVATE -g -O2)
else()
  target_compile_options(dwarf_scope_test PRIVATE -g -O2 -gdwarf-5)
endif()

set(TEST_OUTPUT_DIR "${CMAKE_BINARY_DIR}/tests/dwarf_scope")

add_test(NAME dwarf_scope_filter
  COMMAND ${CMAKE_SOURCE_DIR}/tests/run_dwarf_scope_test.sh
          $<TARGET_FILE:coz>
          $<TARGET_FILE:dwarf_scope_test>
          ${TEST_OUTPUT_DIR})
set_tests_properties(dwarf_scope_filter PROPERTIES
  ENVIRONMENT "PYTHONUNBUFFERED=1")
