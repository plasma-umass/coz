include(FindPkgConfig)
pkg_check_modules(libelfxx REQUIRED libelf++) # >=0.3)
pkg_check_modules(libdwarfxx REQUIRED libdwarf++)#>=0.3)

add_library(libelfin::libelf++ INTERFACE IMPORTED)
set_target_properties(libelfin::libelf++ PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${libelfxx_INCLUDE_DIRS}"
    INTERFACE_LINK_LIBRARIES "${libelfxx_LDFLAGS}"
    INTERFACE_COMPILE_OPTIONS "${libelfxx_CFLAGS};${libelfxx_CFLAGS_OTHER}")
add_library(libelfin::libdwarf++ INTERFACE IMPORTED)
set_target_properties(libelfin::libdwarf++ PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${libdwarfxx_INCLUDE_DIRS}"
    INTERFACE_LINK_LIBRARIES "${libdwarfxx_LDFLAGS}"
    INTERFACE_COMPILE_OPTIONS "${libdwarfxx_CFLAGS};${libdwarfxx_CFLAGS_OTHER}")

if(libelfxx_FOUND AND libdwarfxx_FOUND)
    set(libelfin_FOUND 1)
    set(libelfin_VERSION ${libelfxx_VERSION})
    if(NOT TARGET libelfin::libelfin)
        add_library(libelfin::libelfin INTERFACE IMPORTED)
    endif()
    set_target_properties(libelfin::libelfin PROPERTIES
        INTERFACE_LINK_LIBRARIES "libelfin::libdwarf++;libelfin::libelf++")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(libelfin
    REQUIRED_VARS libelfin_VERSION
    VERSION_VAR libelfin_VERSION)
