# A minimal external project that uses LedgerCore only through its
# installed (or build-tree) CMake package. Built and run by
# tests/package/PackageConsumerTest.cmake; it is not part of the LedgerCore
# build and must not reach into the LedgerCore source tree.
cmake_minimum_required(VERSION 3.20)
project(LedgerCoreConsumer LANGUAGES CXX)

# No CMAKE_CXX_STANDARD here on purpose: LedgerCore::ledgercore must carry
# its own C++17 requirement.
find_package(LedgerCore ${LEDGERCORE_REQUESTED_VERSION} CONFIG REQUIRED)

if (NOT TARGET LedgerCore::ledgercore)
    message(FATAL_ERROR "find_package(LedgerCore) did not define LedgerCore::ledgercore")
endif()
if (DEFINED LEDGERCORE_EXPECTED_VERSION AND NOT LedgerCore_VERSION STREQUAL LEDGERCORE_EXPECTED_VERSION)
    message(FATAL_ERROR "Found LedgerCore ${LedgerCore_VERSION}, expected ${LEDGERCORE_EXPECTED_VERSION}")
endif()

add_executable(ledgercore_consumer main.cpp)
target_link_libraries(ledgercore_consumer PRIVATE LedgerCore::ledgercore)
