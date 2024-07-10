cmake_minimum_required(VERSION 3.13.0)
project(coz VERSION 0.2.2 LANGUAGES C CXX)

enable_testing()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CONAN_CMAKE_SILENT_OUTPUT ON)

include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        58d77fa8070e8cec2dc1ed015d66b454c8d78850 # release-1.12.1
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(
    load_basic_test
    load_basic_test.cpp
)
target_link_libraries(
    load_basic_test
    PRIVATE
    GTest::gtest_main
    #libcoz.so
    coz_nopre
)

include(GoogleTest)
gtest_discover_tests(load_basic_test)
