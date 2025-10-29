cmake_minimum_required(VERSION 3.20)
project(Docmasys LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(DocmasysCAS src/CAS.cpp)
target_include_directories(DocmasysCAS PUBLIC ${CMAKE_SOURCE_DIR}/src)


# --- Source files
add_executable(Docmasys
    src/main.cpp
    src/CAS.cpp
    src/CAS.hpp
)

# OpenSSL
find_package(OpenSSL REQUIRED)
target_link_libraries(Docmasys PRIVATE OpenSSL::Crypto)

# ZSTD via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd)
target_link_libraries(Docmasys PRIVATE PkgConfig::ZSTD)

target_link_libraries(DocmasysCAS
  PUBLIC
    OpenSSL::Crypto
    PkgConfig::ZSTD
)

# Add tests
enable_testing()
add_subdirectory(src/tests)

# --- Platform specifics
if(MSVC)
    target_compile_options(Docmasys PRIVATE /W4)
else()
    target_compile_options(Docmasys PRIVATE -Wall -Wextra -Wpedantic)
endif()

# --- Output folder for built binary
set_target_properties(Docmasys PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)