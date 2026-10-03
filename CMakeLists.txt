cmake_minimum_required(VERSION 3.20)

project(LedgerCore
    VERSION 1.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Emit compile_commands.json for editor/tooling support.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)
include(cmake/CompilerWarnings.cmake)
include(cmake/Sanitizers.cmake)

# Tests (and the GoogleTest download they need) are built by default only
# when LedgerCore is the top-level project, so a parent project that pulls
# LedgerCore in with add_subdirectory()/FetchContent gets just the libraries
# and the CLI.
if (CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(LEDGERCORE_BUILD_TESTS_DEFAULT ON)
else()
    set(LEDGERCORE_BUILD_TESTS_DEFAULT OFF)
endif()
option(LEDGERCORE_BUILD_TESTS "Build the LedgerCore test suite (fetches GoogleTest)" ${LEDGERCORE_BUILD_TESTS_DEFAULT})

add_subdirectory(src)

if (LEDGERCORE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
