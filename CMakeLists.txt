cmake_minimum_required(VERSION 3.13.0)
project(coz VERSION 0.2.2 LANGUAGES C CXX)

if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
        set(CMAKE_OSX_ARCHITECTURES "arm64;arm64e"
            CACHE STRING "Architectures to build on macOS" FORCE)
    endif()
endif()

if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
        set(CMAKE_OSX_ARCHITECTURES "arm64;arm64e"
            CACHE STRING "Architectures to build on macOS" FORCE)
    endif()
endif()

enable_testing()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CONAN_CMAKE_SILENT_OUTPUT ON)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/cmake)

find_package(Threads REQUIRED)

option(USE_SYSTEM_LIBELFIN "Link against libelfin discovered via pkg-config" OFF)

if(USE_SYSTEM_LIBELFIN)
    find_package(libelfin REQUIRED)
else()
    set(LIBELFIN_ELF_SOURCES
        ${PROJECT_SOURCE_DIR}/libelfin/elf/elf.cc
        ${PROJECT_SOURCE_DIR}/libelfin/elf/mmap_loader.cc
        ${PROJECT_SOURCE_DIR}/libelfin/elf/to_string.cc)

    set(LIBELFIN_DWARF_SOURCES
        ${PROJECT_SOURCE_DIR}/libelfin/dwarf/abbrev.cc
        ${PROJECT_SOURCE_DIR}/libelfin/dwarf/attrs.cc
        ${PROJECT_SOURCE_DIR}/libelfin/dwarf/cursor.cc
        ${PROJECT_SOURCE_DIR}/libelfin/dwarf/die.cc
        ${PROJECT_SOURCE_DIR}/libelfin/dwarf/die_str_map.cc
        ${PROJECT_SOURCE_DIR}/libelfin/dwarf/dwarf.cc
        ${PROJECT_SOURCE_DIR}/libelfin/dwarf/elf.cc
        ${PROJECT_SOURCE_DIR}/libelfin/dwarf/expr.cc
        ${PROJECT_SOURCE_DIR}/libelfin/dwarf/line.cc
        ${PROJECT_SOURCE_DIR}/libelfin/dwarf/rangelist.cc
        ${PROJECT_SOURCE_DIR}/libelfin/dwarf/to_string.cc
        ${PROJECT_SOURCE_DIR}/libelfin/dwarf/value.cc)

    add_library(libelfin_internal STATIC
        ${LIBELFIN_ELF_SOURCES}
        ${LIBELFIN_DWARF_SOURCES})
    target_include_directories(libelfin_internal PUBLIC
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/libelfin
        ${PROJECT_SOURCE_DIR}/libelfin/elf
        ${PROJECT_SOURCE_DIR}/libelfin/dwarf)
    target_compile_features(libelfin_internal PUBLIC cxx_std_11)
    set_target_properties(libelfin_internal PROPERTIES
        POSITION_INDEPENDENT_CODE ON)
    add_library(libelfin::libelfin ALIAS libelfin_internal)
endif()

# Always emit debug info so line tables are available when resolving
# progress points, but let the toolchain decide which DWARF version to use.
add_compile_options(-g)
if(APPLE)
    add_compile_options(-gdwarf-4)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -gdwarf-4" CACHE STRING "" FORCE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -gdwarf-4" CACHE STRING "" FORCE)
endif()

option(INSTALL_COZ "Enable installation of coz. (Projects embedding coz may want to turn this OFF.)" ON)

if(INSTALL_COZ)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)
    install(PROGRAMS coz DESTINATION bin)
    install(FILES LICENSE.md DESTINATION licenses)
    configure_package_config_file(
        cmake/coz-profilerConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/coz-profilerConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake)
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/coz-profilerConfigVersion.cmake
        COMPATIBILITY ExactVersion) # SameMajor only applies to versions >= 1.0. Versions 0.x have to match exactly according to semver.org
    install(
        FILES
        ${CMAKE_CURRENT_BINARY_DIR}/coz-profilerConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/coz-profilerConfigVersion.cmake
        DESTINATION
        ${CMAKE_INSTALL_LIBDIR}/cmake)
    install(
        EXPORT coz-profilerTargets
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake
        NAMESPACE coz::)
endif()

add_subdirectory(libcoz)

option(BUILD_BENCHMARKS "Build benchmarks" OFF)
if(BUILD_BENCHMARKS)
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
        message(STATUS "BUILD_BENCHMARKS=ON: defaulting CMAKE_BUILD_TYPE to RelWithDebInfo")
    elseif(NOT (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo"))
        message(STATUS "BUILD_BENCHMARKS=ON: forcing debug info by switching to RelWithDebInfo")
        set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    endif()
    find_package(SQLite3 REQUIRED)
    find_package(BZip2 REQUIRED)
    add_subdirectory(benchmarks)
endif()

add_subdirectory(tests)
