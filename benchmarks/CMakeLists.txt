add_compile_options(-g -O3 -DNDEBUG -gdwarf-3)

function(add_coz_run_target name)
    set(one_value_args "")
    set(multi_value_args COMMAND)
    set(options "")
    cmake_parse_arguments(COZ_RUN "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    add_custom_target(${name}
        COMMENT "Running coz for ${name}"
        COMMAND ${PROJECT_SOURCE_DIR}/coz run --- ${COZ_RUN_COMMAND}
        WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
        DEPENDS coz)
endfunction()

file(GLOB cmake_files */CMakeLists.txt)
foreach(filepath ${cmake_files})
    get_filename_component(dir ${filepath} DIRECTORY)
    add_subdirectory(${dir})
endforeach(filepath)
