add_executable(toy toy.cpp)
target_link_libraries(toy PRIVATE pthread)
# Use DWARF 4 for compatibility with libelfin (DWARF 5 strx forms not fully supported)
target_compile_options(toy PRIVATE -gdwarf-4)

add_coz_run_target(run_toy COMMAND $<TARGET_FILE:toy>)
