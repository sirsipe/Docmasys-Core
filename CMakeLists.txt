cmake_minimum_required(VERSION 3.20)
project(Docmasys LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd)
find_package(SQLite3 REQUIRED)

add_library(DocmasysCAS src/CAS/CAS.cpp)
target_include_directories(DocmasysCAS PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(DocmasysCAS PUBLIC OpenSSL::Crypto PkgConfig::ZSTD)

add_library(DocmasysCore
  src/DB/Database.cpp
  src/Vault.cpp
)
target_include_directories(DocmasysCore PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(DocmasysCore PUBLIC DocmasysCAS SQLite::SQLite3)

add_executable(Docmasys src/main.cpp)
target_link_libraries(Docmasys PRIVATE DocmasysCore)

enable_testing()
add_subdirectory(src/tests)

if(MSVC)
  target_compile_options(Docmasys PRIVATE /W4)
  target_compile_options(DocmasysCore PRIVATE /W4)
else()
  target_compile_options(Docmasys PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(DocmasysCore PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(DocmasysCAS PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(Docmasys PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
