#----------------------------------------------------------------
# Generated CMake target import file.
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "coz::coz" for configuration ""
set_property(TARGET coz::coz APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(coz::coz PROPERTIES
  IMPORTED_COMMON_LANGUAGE_RUNTIME_NOCONFIG ""
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libcoz.so"
  IMPORTED_NO_SONAME_NOCONFIG "TRUE"
  )

list(APPEND _IMPORT_CHECK_TARGETS coz::coz )
list(APPEND _IMPORT_CHECK_FILES_FOR_coz::coz "${_IMPORT_PREFIX}/lib/libcoz.so" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
