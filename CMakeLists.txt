cmake_minimum_required(VERSION 3.20)
project(Docmasys LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL REQUIRED)

if(WIN32)
  find_package(unofficial-sqlite3 CONFIG REQUIRED)
  find_package(zstd CONFIG REQUIRED)
  set(DOCMASYS_SQLITE_TARGET unofficial::sqlite3::sqlite3)
  set(DOCMASYS_ZSTD_TARGET zstd::libzstd)
else()
  find_package(SQLite3 REQUIRED)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd)
  set(DOCMASYS_SQLITE_TARGET SQLite::SQLite3)
  set(DOCMASYS_ZSTD_TARGET PkgConfig::ZSTD)
endif()

add_library(DocmasysCAS src/CAS/CAS.cpp)
target_include_directories(DocmasysCAS PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(DocmasysCAS PUBLIC OpenSSL::Crypto ${DOCMASYS_ZSTD_TARGET})

add_library(DocmasysCore
  src/Common/PathUtils.cpp
  src/DB/Database.cpp
  src/DB/DatabaseProperties.cpp
  src/DB/DatabaseRecords.cpp
  src/DB/DatabaseRelations.cpp
  src/DB/DatabaseWorkspace.cpp
  src/Extensions/Extension.cpp
  src/Vault.cpp
)
target_include_directories(DocmasysCore PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(DocmasysCore PUBLIC DocmasysCAS ${DOCMASYS_SQLITE_TARGET})

add_executable(Docmasys
  src/cli/CommandHelpers.cpp
  src/cli/Commands.cpp
  src/main.cpp
)
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
