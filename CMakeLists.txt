cmake_minimum_required(VERSION 3.13.0)
project(coz VERSION 0.2.2 LANGUAGES C CXX)

enable_testing()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CONAN_CMAKE_SILENT_OUTPUT ON)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/cmake)

find_package(Threads REQUIRED)
find_package(libelfin REQUIRED)

add_compile_options(-gdwarf-3)

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
    if(NOT (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo"))
        message(FATAL_ERROR "Build benchmarks with debug information - use Debug or RelWithDebInfo")
    endif()
    find_package(SQLite3 REQUIRED)
    find_package(BZip2 REQUIRED)
    add_subdirectory(benchmarks)
endif()

