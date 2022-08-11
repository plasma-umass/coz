add_executable(word_count sort-pthread.c word_count-pthread.c)
target_link_libraries(word_count PRIVATE pthread)

add_custom_command(
    COMMENT "Downloading word_count inputs"
    OUTPUT
        word_count_datafiles/word_100MB.txt
        word_count_datafiles/word_10MB.txt
    COMMAND wget -cq http://csl.stanford.edu/~christos/data/word_count.tar.gz
    COMMAND tar xzf word_count.tar.gz)

add_custom_target(word_count_datafiles
    DEPENDS
        word_count_datafiles/word_100MB.txt
        word_count_datafiles/word_10MB.txt)

add_coz_run_target(run_word_count_large
    COMMAND $<TARGET_FILE:word_count> ${CMAKE_CURRENT_BINARY_DIR}/word_count_datafiles/word_100MB.txt)
add_dependencies(run_word_count_large word_count_datafiles)

add_coz_run_target(run_word_count_small
    COMMAND $<TARGET_FILE:word_count> ${CMAKE_CURRENT_BINARY_DIR}/word_count_datafiles/word_10MB.txt)
add_dependencies(run_word_count_small word_count_datafiles)
