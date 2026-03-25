cmake_minimum_required(VERSION 3.16)
project(docmasys_tests CXX)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
)
FetchContent_MakeAvailable(googletest)

add_executable(CAS_tests CAS_tests.cpp)
target_link_libraries(CAS_tests PRIVATE GTest::gtest_main DocmasysCAS)

add_executable(DB_tests DB_tests.cpp)
target_link_libraries(DB_tests PRIVATE GTest::gtest_main DocmasysCore)

add_executable(Vault_tests Vault_tests.cpp)
target_link_libraries(Vault_tests PRIVATE GTest::gtest_main DocmasysCore)

add_executable(CLI_smoke CLI_smoke.cpp)
target_link_libraries(CLI_smoke PRIVATE GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(CAS_tests)
gtest_discover_tests(DB_tests)
gtest_discover_tests(Vault_tests)
gtest_discover_tests(CLI_smoke PROPERTIES ENVIRONMENT "DOCMASYS_BIN=$<TARGET_FILE:Docmasys>")
