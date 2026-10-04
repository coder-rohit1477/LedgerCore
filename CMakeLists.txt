cmake_minimum_required(VERSION 3.20)

project(LedgerCore
    VERSION 1.1.0
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
    set(LEDGERCORE_IS_TOP_LEVEL ON)
else()
    set(LEDGERCORE_IS_TOP_LEVEL OFF)
endif()
option(LEDGERCORE_BUILD_TESTS "Build the LedgerCore test suite (fetches GoogleTest)" ${LEDGERCORE_IS_TOP_LEVEL})

# Install/export the engine library as the LedgerCore CMake package. On by
# default only for a top-level build, so a parent project that uses
# LedgerCore in-tree does not install LedgerCore's headers and libraries
# into its own prefix.
option(LEDGERCORE_INSTALL "Install the LedgerCore library as a CMake package" ${LEDGERCORE_IS_TOP_LEVEL})

add_subdirectory(src)

if (LEDGERCORE_INSTALL)
    include(cmake/LedgerCorePackage.cmake)
endif()

if (LEDGERCORE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
