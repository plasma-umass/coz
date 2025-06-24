
####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was coz-profilerConfig.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

message(INFO " ${COZ_INCLUDE_DIR} ${COZ_LIBRARY} ${COZ_FOUND}")

# Use find_dependency to find dependend packages.

include(${CMAKE_CURRENT_LIST_DIR}/coz-profilerTargets.cmake)

add_executable(coz::profiler IMPORTED)

# This includes the lib file
get_target_property(_COZ_LIBARRY_PATH coz::coz IMPORTED_LOCATION)
# {INSTALLATION}/lib/libcoz.so
get_filename_component(_IMPORT_PREFIX "${_COZ_LIBRARY_PATH}" PATH)
# {INSTALLATION}/lib
get_filename_component(_IMPORT_PREFIX "${_COZ_LIBARRY_PATH}" PATH)
# {INSTALLATION}
set_property(TARGET coz::profiler PROPERTY IMPORTED_LOCATION ${_COZ_LIBARRY_PATH})
# Cleanup temporary variables
set(_COZ_LIBARRY_PATH)

