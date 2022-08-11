add_executable(histogram histogram-pthread.c)
target_link_libraries(histogram PRIVATE pthread)

add_custom_command(
    COMMENT "Downloading histogram inputs"
    OUTPUT
        histogram_datafiles/small.bmp
        histogram_datafiles/large.bmp
    COMMAND wget -cq http://csl.stanford.edu/~christos/data/histogram.tar.gz
    COMMAND tar xzf histogram.tar.gz)

add_custom_target(histogram_datafiles
    DEPENDS
        histogram_datafiles/small.bmp
        histogram_datafiles/large.bmp)

add_coz_run_target(run_histogram_large
    COMMAND $<TARGET_FILE:histogram> ${CMAKE_CURRENT_BINARY_DIR}/histogram_datafiles/large.bmp)
add_dependencies(run_histogram_large histogram_datafiles)

add_coz_run_target(run_histogram_small
    COMMAND $<TARGET_FILE:histogram> ${CMAKE_CURRENT_BINARY_DIR}/histogram_datafiles/small.bmp)
add_dependencies(run_histogram_small histogram_datafiles)
