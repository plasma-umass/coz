add_executable(pbzip2 BZ2StreamScanner.cpp ErrorContext.cpp pbzip2.cpp)
target_link_libraries(pbzip2 PRIVATE bz2 pthread)
target_compile_options(pbzip2 PRIVATE -Wno-format)

set(datafiles ${CMAKE_CURRENT_BINARY_DIR}/../linear_regression/linear_regression_datafiles)

add_coz_run_target(run_pbzip2_large
    COMMAND $<TARGET_FILE:pbzip2> -c < ${datafiles}/key_file_500MB.txt > ${CMAKE_CURRENT_BINARY_DIR}/key_file_500MB_compressed.bz2)
add_dependencies(run_pbzip2_large linear_regression_datafiles)

add_coz_run_target(run_pbzip2_small
    COMMAND $<TARGET_FILE:pbzip2> -c < ${datafiles}/key_file_50MB.txt > ${CMAKE_CURRENT_BINARY_DIR}/key_file_50MB_compressed.bz2)
add_dependencies(run_pbzip2_small linear_regression_datafiles)

