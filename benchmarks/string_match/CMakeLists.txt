add_executable(string_match string_match-pthread.c)
target_link_libraries(string_match PRIVATE pthread)

add_custom_command(
    COMMENT "Downloading string_match inputs"
    OUTPUT
        string_match_datafiles/key_file_500MB.txt
        string_match_datafiles/key_file_50MB.txt
    COMMAND wget -cq http://csl.stanford.edu/~christos/data/string_match.tar.gz
    COMMAND tar xzf string_match.tar.gz)

add_custom_target(string_match_datafiles
    DEPENDS
        string_match_datafiles/key_file_500MB.txt
        string_match_datafiles/key_file_50MB.txt)

add_coz_run_target(run_string_match_large
    COMMAND $<TARGET_FILE:string_match> ${CMAKE_CURRENT_BINARY_DIR}/string_match_datafiles/key_file_500MB.txt)
add_dependencies(run_string_match_large string_match_datafiles)

add_coz_run_target(run_string_match_small
    COMMAND $<TARGET_FILE:string_match> ${CMAKE_CURRENT_BINARY_DIR}/string_match_datafiles/key_file_50MB.txt)
add_dependencies(run_string_match_small string_match_datafiles)
