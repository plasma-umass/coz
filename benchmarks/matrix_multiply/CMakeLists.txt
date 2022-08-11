add_executable(matrix_multiply matrix_multiply-pthread.c)
target_link_libraries(matrix_multiply PRIVATE pthread)
target_compile_options(matrix_multiply PRIVATE -Wno-format)

add_custom_command(
    COMMENT "Generating bench input"
    OUTPUT ${PROJECT_BINARY_DIR}/matrix_file_A_1000.txt ${PROJECT_BINARY_DIR}/matrix_file_B_1000.txt
    BYPRODUCTS ${PROJECT_BINARY_DIR}/matrix_file_out_pthreads_1000.txt
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/matrix_multiply 1000 -create_files > /dev/null
    WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
    DEPENDS matrix_multiply)
add_custom_target(matrix_multiply_large_datafiles
    DEPENDS ${PROJECT_BINARY_DIR}/matrix_file_A_1000.txt ${PROJECT_BINARY_DIR}/matrix_file_B_1000.txt)

add_custom_command(
    COMMENT "Generating test input"
    OUTPUT ${PROJECT_BINARY_DIR}/matrix_file_A_400.txt ${PROJECT_BINARY_DIR}/matrix_file_B_400.txt
    BYPRODUCTS ${PROJECT_BINARY_DIR}/matrix_file_out_pthreads_400.txt
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/matrix_multiply 400 -create_files > /dev/null
    WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
    DEPENDS matrix_multiply)
add_custom_target(matrix_multiply_small_datafiles
    DEPENDS ${PROJECT_BINARY_DIR}/matrix_file_A_400.txt ${PROJECT_BINARY_DIR}/matrix_file_B_400.txt)

add_coz_run_target(run_matrix_multiply_large
    COMMAND $<TARGET_FILE:matrix_multiply> 1000 > /dev/null)
add_dependencies(run_matrix_multiply_large matrix_multiply_large_datafiles)

add_coz_run_target(run_matrix_multiply_small
    COMMAND $<TARGET_FILE:matrix_multiply> 400 > /dev/null)
add_dependencies(run_matrix_multiply_small matrix_multiply_small_datafiles)
