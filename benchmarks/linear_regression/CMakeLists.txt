add_executable(linear_regression linear_regression-pthread.c)
target_link_libraries(linear_regression PRIVATE pthread)

add_custom_command(
    COMMENT "Downloading linear_regression inputs"
    OUTPUT
        linear_regression_datafiles/key_file_500MB.txt
        linear_regression_datafiles/key_file_50MB.txt
    COMMAND wget -cq http://csl.stanford.edu/~christos/data/linear_regression.tar.gz
    COMMAND tar xzf linear_regression.tar.gz)

add_custom_target(linear_regression_datafiles
    DEPENDS
        linear_regression_datafiles/key_file_500MB.txt
        linear_regression_datafiles/key_file_50MB.txt)

add_coz_run_target(run_linear_regression_large
    COMMAND $<TARGET_FILE:linear_regression> ${CMAKE_CURRENT_BINARY_DIR}/linear_regression_datafiles/key_file_500MB.txt)
add_dependencies(run_linear_regression_large linear_regression_datafiles)

add_coz_run_target(run_linear_regression_small
    COMMAND $<TARGET_FILE:linear_regression> ${CMAKE_CURRENT_BINARY_DIR}/linear_regression_datafiles/key_file_50MB.txt)
add_dependencies(run_linear_regression_small linear_regression_datafiles)
