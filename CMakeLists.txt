cmake_minimum_required(VERSION 3.13.0)
project(coz VERSION 0.2.5 LANGUAGES C CXX)

enable_testing()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CONAN_CMAKE_SILENT_OUTPUT ON)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/cmake)

find_package(Threads REQUIRED)

# ============== LIEF library for cross-platform binary parsing ==============
option(USE_SYSTEM_LIEF "Link against LIEF discovered via find_package" OFF)

if(USE_SYSTEM_LIEF)
    find_package(LIEF REQUIRED)
else()
    include(FetchContent)

    set(LIEF_GIT_TAG "0.17.0" CACHE STRING
        "Git tag/branch/commit for LIEF")
    set(LIEF_GIT_REPO "https://github.com/lief-project/LIEF.git" CACHE STRING
        "Git repository URL for LIEF")

    # Disable LIEF features we don't need to speed up build
    set(LIEF_PYTHON_API OFF CACHE BOOL "" FORCE)
    set(LIEF_C_API OFF CACHE BOOL "" FORCE)
    set(LIEF_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LIEF_TESTS OFF CACHE BOOL "" FORCE)
    set(LIEF_DOC OFF CACHE BOOL "" FORCE)
    set(LIEF_LOGGING OFF CACHE BOOL "" FORCE)
    set(LIEF_ENABLE_JSON OFF CACHE BOOL "" FORCE)
    # Only enable formats we need
    set(LIEF_ELF ON CACHE BOOL "" FORCE)
    set(LIEF_PE OFF CACHE BOOL "" FORCE)
    set(LIEF_MACHO ON CACHE BOOL "" FORCE)
    set(LIEF_OAT OFF CACHE BOOL "" FORCE)
    set(LIEF_DEX OFF CACHE BOOL "" FORCE)
    set(LIEF_VDEX OFF CACHE BOOL "" FORCE)
    set(LIEF_ART OFF CACHE BOOL "" FORCE)
    set(LIEF_COFF OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        LIEF
        GIT_REPOSITORY ${LIEF_GIT_REPO}
        GIT_TAG        ${LIEF_GIT_TAG}
    )

    FetchContent_MakeAvailable(LIEF)
    message(STATUS "Fetched LIEF ${LIEF_GIT_TAG}")
endif()

# ============== libelfin for DWARF parsing ==============
option(USE_SYSTEM_LIBELFIN "Link against libelfin discovered via pkg-config" OFF)

if(USE_SYSTEM_LIBELFIN)
    find_package(libelfin REQUIRED)
else()
    # Check for local libelfin directory first (may contain patches/modifications)
    if(EXISTS "${PROJECT_SOURCE_DIR}/libelfin/elf/elf.cc")
        set(libelfin_SOURCE_DIR "${PROJECT_SOURCE_DIR}/libelfin")
        message(STATUS "Using local libelfin from ${libelfin_SOURCE_DIR}")
        set(LIBELFIN_LOCAL TRUE)
    else()
        # Fetch libelfin from GitHub
        include(FetchContent)

        set(LIBELFIN_GIT_TAG "origin/improved_dwarf5_support" CACHE STRING
            "Git tag/branch/commit for libelfin (default: DWARF5 support with pre-built to_string.cc)")
        set(LIBELFIN_GIT_REPO "https://github.com/plasma-umass/libelfin.git" CACHE STRING
            "Git repository URL for libelfin")

        FetchContent_Declare(
            libelfin
            GIT_REPOSITORY ${LIBELFIN_GIT_REPO}
            GIT_TAG        ${LIBELFIN_GIT_TAG}
        )

        FetchContent_GetProperties(libelfin)
        if(NOT libelfin_POPULATED)
            FetchContent_Populate(libelfin)
        endif()
        message(STATUS "Fetched libelfin to ${libelfin_SOURCE_DIR}")
        set(LIBELFIN_LOCAL FALSE)
    endif()

    set(LIBELFIN_ELF_SOURCES
        ${libelfin_SOURCE_DIR}/elf/elf.cc
        ${libelfin_SOURCE_DIR}/elf/mmap_loader.cc
        ${libelfin_SOURCE_DIR}/elf/to_string.cc)

    set(LIBELFIN_DWARF_SOURCES
        ${libelfin_SOURCE_DIR}/dwarf/abbrev.cc
        ${libelfin_SOURCE_DIR}/dwarf/attrs.cc
        ${libelfin_SOURCE_DIR}/dwarf/cursor.cc
        ${libelfin_SOURCE_DIR}/dwarf/die.cc
        ${libelfin_SOURCE_DIR}/dwarf/die_str_map.cc
        ${libelfin_SOURCE_DIR}/dwarf/dwarf.cc
        ${libelfin_SOURCE_DIR}/dwarf/elf.cc
        ${libelfin_SOURCE_DIR}/dwarf/expr.cc
        ${libelfin_SOURCE_DIR}/dwarf/line.cc
        ${libelfin_SOURCE_DIR}/dwarf/rangelist.cc
        ${libelfin_SOURCE_DIR}/dwarf/to_string.cc
        ${libelfin_SOURCE_DIR}/dwarf/value.cc)

    add_library(libelfin_internal STATIC
        ${LIBELFIN_ELF_SOURCES}
        ${LIBELFIN_DWARF_SOURCES})

    target_include_directories(libelfin_internal PUBLIC
        ${libelfin_SOURCE_DIR}
        ${libelfin_SOURCE_DIR}/elf
        ${libelfin_SOURCE_DIR}/dwarf)
    target_compile_features(libelfin_internal PUBLIC cxx_std_11)
    set_target_properties(libelfin_internal PROPERTIES
        POSITION_INDEPENDENT_CODE ON)
    add_library(libelfin::libelfin ALIAS libelfin_internal)
endif()

# Always emit debug info so line tables are available when resolving
# progress points, but let the toolchain decide which DWARF version to use.
add_compile_options(-g)

option(INSTALL_COZ "Enable installation of coz. (Projects embedding coz may want to turn this OFF.)" ON)

if(INSTALL_COZ)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)
    install(PROGRAMS coz DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES LICENSE.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
    install(FILES pyproject.toml DESTINATION ${CMAKE_INSTALL_DATADIR}/coz)

    # Install the viewer web application
    install(DIRECTORY viewer/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/coz/viewer
        PATTERN "node_modules" EXCLUDE
        PATTERN "ts" EXCLUDE
        PATTERN ".claude" EXCLUDE
        PATTERN "*.ts" EXCLUDE
        PATTERN "tsconfig.json" EXCLUDE
        PATTERN "package*.json" EXCLUDE
        PATTERN "Makefile" EXCLUDE
        PATTERN "*.map" EXCLUDE)
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

if(APPLE)
    # 1. Specify the location of private frameworks to the linker
    # This adds the -iframework /System/Library/PrivateFrameworks flag
    target_link_options(coz PRIVATE "-iframework" "/System/Library/PrivateFrameworks")

    # 2. Link against the specific (private) framework
    # The name is likely 'Kperf' based on the symbol names.
    # The quotes are important so CMake treats "-framework Kperf" as a single argument.
    target_link_libraries(coz PRIVATE "-framework Kperf")
endif()
