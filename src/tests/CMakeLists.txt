cmake_minimum_required(VERSION 3.16)
project(cas_tests CXX)
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
)
FetchContent_MakeAvailable(googletest)

add_executable(CAS_tests CAS_tests.cpp)
target_link_libraries(CAS_tests PRIVATE GTest::gtest_main DocmasysCAS)

include(GoogleTest)
gtest_discover_tests(CAS_tests)
