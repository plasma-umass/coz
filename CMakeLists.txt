cmake_minimum_required(VERSION 3.4)
project(coz C CXX)

enable_testing()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_BINARY_DIR})

include(GNUInstallDirs)
find_package(Threads REQUIRED)
find_package(libelfin REQUIRED)

file(GLOB_RECURSE sources libcoz/*.cpp)
file(GLOB_RECURSE headers include/*.h)

install(PROGRAMS coz DESTINATION bin)
install(FILES LICENSE.md DESTINATION licenses)

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

configure_file(coz-profilerConfig.cmake.in coz-profilerConfig.cmake @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/coz-profilerConfig.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake)