cmake_minimum_required(VERSION 3.1)
project(coz C CXX)

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
    find_package(SQLite3 REQUIRED)
    find_package(BZip2 REQUIRED)
    add_subdirectory(benchmarks)
endif()

configure_file(coz-profilerConfig.cmake.in coz-profilerConfig.cmake @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/coz-profilerConfig.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake)
